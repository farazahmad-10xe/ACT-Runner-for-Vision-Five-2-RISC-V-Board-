#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <uart.log> [out_dir]" >&2
  exit 1
fi

in_log="$1"
out_dir="${2:-act_report}"

mkdir -p "$out_dir"

clean_log="$out_dir/clean.log"
events_log="$out_dir/events.log"
summary_txt="$out_dir/summary.txt"
suite_summary_txt="$out_dir/suite_summary.txt"
per_case_csv="$out_dir/per_case_report.csv"
sig_words="$out_dir/signature_words32.log"
sig64_file="$out_dir/signature.sig"
act_suite_txt="$out_dir/act_suite_report.txt"
per_case_dir="$out_dir/per_case_act"
tmp_case_dir="$out_dir/.tmp_case_parse"

rm -rf "$per_case_dir"
mkdir -p "$per_case_dir"
rm -rf "$tmp_case_dir"
mkdir -p "$tmp_case_dir"

# Normalize log for parsing.
tr -d '\000' < "$in_log" | sed 's/\r$//' > "$clean_log"

rg -n "RVCP-SUMMARY|PASS|FAIL|\\[ACT\\]|\\[SIG\\]|\\[MON\\]|\\[TRAP\\]|\\[CTX\\]|\\[REG\\]|\\[CSR\\]|\\[MEMDUMP\\]" "$clean_log" > "$events_log" || true
rg "^\\[SIG\\] 0x" "$clean_log" > "$sig_words" || true

# Per-case extraction from runner report lines.
awk '
  BEGIN {
    FS=" ";
    print "name,status,rc,tohost_addr,tohost_value,sig_begin,sig_end,sig_bytes,exit_pc";
  }
  /^\[CASE\] REPORT name=/ {
    name=""; status=""; rc=""; tohost_addr=""; tohost_value=""; sig_begin=""; sig_end=""; sig_bytes=""; exit_pc="";
    for (i = 1; i <= NF; i++) {
      if ($i ~ /^name=/) name=substr($i, 6);
      else if ($i ~ /^status=/) status=substr($i, 8);
      else if ($i ~ /^rc=/) rc=substr($i, 4);
      else if ($i ~ /^tohost_addr=/) tohost_addr=substr($i, 13);
      else if ($i ~ /^tohost_value=/) tohost_value=substr($i, 14);
      else if ($i ~ /^sig_begin=/) sig_begin=substr($i, 11);
      else if ($i ~ /^sig_end=/) sig_end=substr($i, 9);
      else if ($i ~ /^sig_bytes=/) sig_bytes=substr($i, 11);
      else if ($i ~ /^exit_pc=/) exit_pc=substr($i, 9);
    }
    if (exit_pc == "") exit_pc="0x0000000000000000";
    print name "," status "," rc "," tohost_addr "," tohost_value "," sig_begin "," sig_end "," sig_bytes "," exit_pc;
  }
' "$clean_log" > "$per_case_csv"

# Fallback parser for noisy logs where [CASE] REPORT may be mangled by reset.
if [[ "$(awk 'END{print (NR>0?NR-1:0)}' "$per_case_csv")" -eq 0 ]]; then
  awk '
    BEGIN {
      print "name,status,rc,tohost_addr,tohost_value,sig_begin,sig_end,sig_bytes,exit_pc";
      name=""; tohost_addr=""; sig_begin=""; sig_end="";
    }
    function flush_case(   sig_bytes,status,rc,tohost_value,exit_pc) {
      if (name == "") return;
      status = "ERROR";
      rc = "0x0000000000000000";
      tohost_value = "0x0000000000000000";
      if (summary_status == "PASSED") {
        status = "PASS";
        tohost_value = "0x0000000000000001";
      } else if (summary_status == "FAILED") {
        status = "FAIL";
      }
      if (result_status != "") status = result_status;
      if (result_tohost != "") tohost_value = result_tohost;
      sig_bytes = "0x0000000000000000";
      if (sig_begin != "" && sig_end != "") {
        b = strtonum(sig_begin);
        e = strtonum(sig_end);
        if (e > b) sig_bytes = sprintf("0x%016x", e - b);
      }
      if (tohost_addr == "") tohost_addr = "0x0000000000000000";
      if (sig_begin == "") sig_begin = "0x0000000000000000";
      if (sig_end == "") sig_end = "0x0000000000000000";
      exit_pc = "0x0000000000000000";
      print name "," status "," rc "," tohost_addr "," tohost_value "," sig_begin "," sig_end "," sig_bytes "," exit_pc;
    }
    /^\[CASE\] START name=/ {
      flush_case();
      name = substr($0, index($0, "name=") + 5);
      tohost_addr = ""; sig_begin = ""; sig_end = "";
      summary_status = "";
      result_status = "";
      result_tohost = "";
      next;
    }
    /^\[ACT\] tohost=0x/ {
      if (name != "") tohost_addr = substr($0, index($0, "0x"));
      next;
    }
    /^\[ACT\] sig_begin=0x/ {
      if (name != "") {
        match($0, /sig_begin=(0x[0-9a-fA-F]+)/, m1);
        match($0, /sig_end=(0x[0-9a-fA-F]+)/, m2);
        if (m1[1] != "") sig_begin = m1[1];
        if (m2[1] != "") sig_end = m2[1];
      }
      next;
    }
    /^\[CASE\] RESULT name=/ {
      if (name != "") {
        if (index($0, " status=PASS ") > 0) result_status = "PASS";
        else if (index($0, " status=FAIL ") > 0) result_status = "FAIL";
        else if (index($0, " status=TIMEOUT ") > 0) result_status = "TIMEOUT";
        else if (index($0, " status=ERROR ") > 0) result_status = "ERROR";
        match($0, /tohost=(0x[0-9a-fA-F]+)/, mt);
        if (mt[1] != "") result_tohost = mt[1];
      }
      next;
    }
    /^RVCP-SUMMARY: Test File "/ {
      if (name != "") {
        if (index($0, "PASSED") > 0) summary_status = "PASSED";
        else if (index($0, "FAILED") > 0) summary_status = "FAILED";
      }
      next;
    }
    END { flush_case(); }
  ' "$clean_log" > "$per_case_csv"
fi

# Add synthetic TIMEOUT rows for tests that STARTed but never emitted [CASE] REPORT.
# Determine expected list once (used both for TIMEOUT filtering and SKIPPED generation).
expected_list_file="${EXPECTED_LIST:-}"
if [[ -z "$expected_list_file" && -f "ext_lists/ALL.list" ]]; then
  expected_list_file="ext_lists/ALL.list"
fi
expected_names_filter=""
if [[ -n "$expected_list_file" && -f "$expected_list_file" ]]; then
  expected_names_filter="$(mktemp)"
  sed 's#.*/##; s/\.elf$//' "$expected_list_file" > "$expected_names_filter"
fi

start_meta_csv="$out_dir/.start_meta.csv"
awk '
  BEGIN { OFS=","; print "name,tohost_addr,sig_begin,sig_end,timeout_seen,trap_seen"; in_case=0; }
  function flush_case(   ) {
    if (!in_case) return;
    print name, tohost_addr, sig_begin, sig_end, timeout_seen, trap_seen;
  }
  /^\[CASE\] START name=/ {
    flush_case();
    in_case=1;
    name=substr($0, index($0, "name=") + 5);
    tohost_addr="0x0000000000000000";
    sig_begin="0x0000000000000000";
    sig_end="0x0000000000000000";
    timeout_seen=0;
    trap_seen=0;
    next;
  }
  /^\[ACT\] tohost=0x/ {
    if (in_case) tohost_addr=substr($0, index($0, "0x"));
    next;
  }
  /^\[ACT\] sig_begin=0x/ {
    if (in_case) {
      match($0, /sig_begin=(0x[0-9a-fA-F]+)/, m1);
      match($0, /sig_end=(0x[0-9a-fA-F]+)/, m2);
      if (m1[1] != "") sig_begin=m1[1];
      if (m2[1] != "") sig_end=m2[1];
    }
    next;
  }
  /^\[RST\] test timeout/ { if (in_case) timeout_seen=1; next; }
  /^\[TRAP\]/ { if (in_case) trap_seen=1; next; }
  END { flush_case(); }
' "$clean_log" > "$start_meta_csv"

tmp_aug_csv="$(mktemp)"
awk -F, '
  BEGIN { OFS="," }
  FNR==NR {
    if (NR>1) rep_cnt[$1]++;
    next;
  }
  NR==1 { next; }
  {
    name=$1; tohost=$2; sb=$3; se=$4; tmo=$5; trp=$6;
    seen_cnt[name]++;
    if (seen_cnt[name] > rep_cnt[name]) {
      status=((tmo=="1" || trp=="1") ? "TIMEOUT" : "ERROR");
      rc="0x0000000000000000";
      tohost_val="0x0000000000000000";
      sig_bytes="0x0000000000000000";
      if (sb != "" && se != "" && sb != "0x0000000000000000" && se != "0x0000000000000000") {
        b=strtonum(sb); e=strtonum(se);
        if (e > b) sig_bytes=sprintf("0x%016x", e-b);
      }
      print name, status, rc, tohost, tohost_val, sb, se, sig_bytes, "0x0000000000000000";
    }
  }
' "$per_case_csv" "$start_meta_csv" > "$tmp_aug_csv"
if [[ -n "$expected_names_filter" && -f "$expected_names_filter" && -s "$tmp_aug_csv" ]]; then
  tmp_aug_filtered="$(mktemp)"
  awk -F, '
    FNR==NR { ok[$1]=1; next; }
    ($1 in ok) { print; }
  ' "$expected_names_filter" "$tmp_aug_csv" > "$tmp_aug_filtered"
  mv "$tmp_aug_filtered" "$tmp_aug_csv"
fi
if [[ -s "$tmp_aug_csv" ]]; then
  cat "$tmp_aug_csv" >> "$per_case_csv"
fi
rm -f "$tmp_aug_csv"

# Add synthetic SKIPPED rows for tests expected but never observed.
if [[ -n "$expected_list_file" && -f "$expected_list_file" ]]; then
  expected_names_file="$expected_names_filter"
  if [[ -z "$expected_names_file" || ! -f "$expected_names_file" ]]; then
    expected_names_file="$(mktemp)"
    sed 's#.*/##; s/\.elf$//' "$expected_list_file" > "$expected_names_file"
  fi
  tmp_skip_csv="$(mktemp)"
  awk -F, '
    BEGIN { OFS="," }
    FNR==NR {
      if (NR>1) actual[$1]++;
      next;
    }
    {
      expected[$1]++;
    }
    END {
      for (n in expected) {
        missing=expected[n]-actual[n];
        for (i=0; i<missing; i++) {
          print n, "SKIPPED", "0x0000000000000000", "0x0000000000000000", "0x0000000000000000", "0x0000000000000000", "0x0000000000000000", "0x0000000000000000", "0x0000000000000000";
        }
      }
    }
  ' "$per_case_csv" "$expected_names_file" > "$tmp_skip_csv"
  if [[ -s "$tmp_skip_csv" ]]; then
    cat "$tmp_skip_csv" >> "$per_case_csv"
  fi
  rm -f "$tmp_skip_csv"
fi
rm -f "${expected_names_filter:-}"

tmp_attempt_csv="$(mktemp)"
awk -F, '
  BEGIN { OFS="," }
  NR==1 {
    print "name", "attempt", "status", "rc", "tohost_addr", "tohost_value", "sig_begin", "sig_end", "sig_bytes", "exit_pc";
    next;
  }
  {
    attempt[$1]++;
    print $1, attempt[$1], $2, $3, $4, $5, $6, $7, $8, $9;
  }
' "$per_case_csv" > "$tmp_attempt_csv"
mv "$tmp_attempt_csv" "$per_case_csv"

total_cases="$(awk 'END{print (NR>0?NR-1:0)}' "$per_case_csv")"
pass_cases="$(awk -F, 'NR>1 && $3=="PASS"{c++} END{print c+0}' "$per_case_csv")"
fail_cases="$(awk -F, 'NR>1 && $3=="FAIL"{c++} END{print c+0}' "$per_case_csv")"
timeout_cases="$(awk -F, 'NR>1 && $3=="TIMEOUT"{c++} END{print c+0}' "$per_case_csv")"
error_cases="$(awk -F, 'NR>1 && $3=="ERROR"{c++} END{print c+0}' "$per_case_csv")"
skipped_cases="$(awk -F, 'NR>1 && $3=="SKIPPED"{c++} END{print c+0}' "$per_case_csv")"

cat > "$suite_summary_txt" <<EOF
input_log=$in_log
total_cases=$total_cases
pass_cases=$pass_cases
fail_cases=$fail_cases
timeout_cases=$timeout_cases
error_cases=$error_cases
skipped_cases=$skipped_cases
per_case_report=$per_case_csv
EOF

test_file="$(sed -n 's/.*RVCP-SUMMARY: Test File "\(.*\)":.*/\1/p' "$clean_log" | tail -n1)"
result="$( (rg -n "^(PASS|FAIL)$" "$clean_log" || true) | tail -n1 | sed 's/^[0-9]*://')"
tohost_line="$( (sed -n 's/^\[MON\] tohost addr=\(0x[0-9a-fA-F]\+\) value=\(0x[0-9a-fA-F]\+\)$/\1 \2/p' "$clean_log" || true) | tail -n1)"
sig_range="$( (sed -n 's/^\[SIG\] begin=\(0x[0-9a-fA-F]\+\) end=\(0x[0-9a-fA-F]\+\) bytes=\(0x[0-9a-fA-F]\+\)$/\1 \2 \3/p' "$clean_log" || true) | tail -n1)"
truncated="no"
if rg -q "^\\[SIG\\] \\.\\.\\.truncated\\.\\.\\.$" "$clean_log"; then
  truncated="yes"
fi

tohost_addr=""
tohost_val=""
if [[ -n "$tohost_line" ]]; then
  tohost_addr="${tohost_line%% *}"
  tohost_val="${tohost_line##* }"
fi

sig_begin=""
sig_end=""
sig_bytes=""
if [[ -n "$sig_range" ]]; then
  sig_begin="$(echo "$sig_range" | awk '{print $1}')"
  sig_end="$(echo "$sig_range" | awk '{print $2}')"
  sig_bytes="$(echo "$sig_range" | awk '{print $3}')"
fi

cat > "$summary_txt" <<EOF
input_log=$in_log
test_file=${test_file:-unknown}
result=${result:-unknown}
tohost_addr=${tohost_addr:-unknown}
tohost_value=${tohost_val:-unknown}
signature_begin=${sig_begin:-unknown}
signature_end=${sig_end:-unknown}
signature_bytes=${sig_bytes:-unknown}
signature_truncated=$truncated
events_log=$events_log
signature_words32_log=$sig_words
signature_sig_file=$sig64_file
EOF

# Convert "[SIG] addr : 0xNNNNNNNN" pairs into ACT-style 64-bit lines:
# line = hi32(addr+4) + lo32(addr), constrained to [sig_begin, sig_end).
awk -v sig_begin="${sig_begin:-}" -v sig_end="${sig_end:-}" '
  /^\[SIG\] 0x/ {
    addr=$2
    val=$4
    gsub(":", "", addr)
    gsub("0x", "", addr)
    gsub("0x", "", val)
    a=strtonum("0x" addr)
    if (sig_begin != "" && sig_end != "") {
      b=strtonum(sig_begin)
      e=strtonum(sig_end)
      if (a < b || a >= e) next
    }
    v[a]=val
    if (min==0 || a<min) min=a
    if (a>max) max=a
  }
  END {
    if (sig_begin != "" && sig_end != "") {
      b=strtonum(sig_begin)
      e=strtonum(sig_end)
      if (e <= b) exit
      for (a=b; a<e; a+=8) {
        lo=(a in v)?v[a]:"00000000"
        hi=((a+4) in v)?v[a+4]:"00000000"
        print hi lo
      }
      exit
    }
    if (min==0 && max==0) exit
    for (a=min; a<=max; a+=8) {
      lo=(a in v)?v[a]:"00000000"
      hi=((a+4) in v)?v[a+4]:"00000000"
      print hi lo
    }
  }
' "$clean_log" > "$sig64_file"

# Some DUT logs include duplicated guard qwords at line 2 and final line.
# Normalize only when this exact pattern is detected.
if [[ -s "$sig64_file" ]]; then
  first_line="$(sed -n '1p' "$sig64_file")"
  second_line="$(sed -n '2p' "$sig64_file")"
  last_line="$(sed -n '$p' "$sig64_file")"
  if [[ -n "$first_line" && "$second_line" == "$first_line" && "$last_line" == "$first_line" ]]; then
    tmp_norm="$(mktemp)"
    sed '2d;$d' "$sig64_file" > "$tmp_norm"
    mv "$tmp_norm" "$sig64_file"
  fi
fi

# Split signature/debug lines by case index (same order as [CASE] START).
awk -v out="$tmp_case_dir" '
  /^\[CASE\] START name=/ { idx++; in_memdump=0; next; }
  idx > 0 {
    if ($0 ~ /^\[SIGQ\] /) print >> (out "/case_" sprintf("%03d", idx) ".sigq");
    if ($0 ~ /^\[SIGB\] /) print >> (out "/case_" sprintf("%03d", idx) ".sigb");
    if ($0 ~ /^\[CASE\] / || $0 ~ /^RVCP-SUMMARY:/ || $0 ~ /^\[ACT\]/ || $0 ~ /^\[MON\]/ || $0 ~ /^\[TRAP\]/ || $0 ~ /^\[RST\]/ || $0 ~ /^\[CTX\]/ || $0 ~ /^\[REG\]/ || $0 ~ /^\[CSR\]/ || $0 ~ /^\[MEMDUMP\]/) {
      print >> (out "/case_" sprintf("%03d", idx) ".events");
      in_memdump = ($0 ~ /^\[MEMDUMP\]/);
    } else if (in_memdump && ($0 ~ /^=> 0x[0-9a-fA-F]+[[:space:]]*:/ || $0 ~ /^   0x[0-9a-fA-F]+[[:space:]]*:/ || $0 ~ /^  0x[0-9a-fA-F]+[[:space:]]*:/)) {
      print >> (out "/case_" sprintf("%03d", idx) ".events");
    } else {
      in_memdump = 0;
    }
  }
' "$clean_log"

# Emit per-case ACT-style reports and signature files.
{
  IFS=,
  read -r _h1 _h2 _h3 _h4 _h5 _h6 _h7 _h8 _h9 _h10 || true
  idx=0
  while IFS=, read -r name attempt status rc tohost_addr tohost_value sig_begin sig_end sig_bytes exit_pc; do
    idx=$((idx + 1))
    safe_name="$(echo "$name" | sed 's/[^A-Za-z0-9_.-]/_/g')"
    case_root="$per_case_dir/$(printf "%03d" "$idx")_${safe_name}__attempt$(printf "%03d" "$attempt")"
    mkdir -p "$case_root"

    status_word="FAILED"
    if [[ "$status" == "PASS" ]]; then
      status_word="PASSED"
    fi

    cat > "$case_root/case_report.txt" <<EOF
RVCP-REPORT: Test File "${name}.S"
ATTEMPT     : $attempt
STATUS      : $status
RC          : $rc
TOHOST_ADDR : $tohost_addr
TOHOST_VAL  : $tohost_value
SIG_BEGIN   : $sig_begin
SIG_END     : $sig_end
SIG_BYTES   : $sig_bytes
EXIT_PC     : $exit_pc
RVCP-SUMMARY: Test File "${name}.S": $status_word
EOF

    sigq_file="$tmp_case_dir/case_$(printf "%03d" "$idx").sigq"
    sigb_file="$tmp_case_dir/case_$(printf "%03d" "$idx").sigb"
    ev_file="$tmp_case_dir/case_$(printf "%03d" "$idx").events"

    : > "$case_root/signature_addr_val.log"
    : > "$case_root/signature.sig"

    if [[ -s "$sigq_file" ]]; then
      awk '
        /^\[SIGQ\] 0x/ {
          addr=$2; gsub(":", "", addr);
          val=$4;
          print addr " " val;
          gsub(/^0x/, "", val);
          print tolower(val);
        }
      ' "$sigq_file" > "$case_root/.sig_tmp"
      awk 'NR%2==1{print}' "$case_root/.sig_tmp" > "$case_root/signature_addr_val.log"
      awk 'NR%2==0{print}' "$case_root/.sig_tmp" > "$case_root/signature.sig"
      rm -f "$case_root/.sig_tmp"

      # Normalize duplicated guard qwords if pattern appears:
      # line1 == line2 and last == line1  -> drop line2 and last.
      if [[ -s "$case_root/signature.sig" ]]; then
        first_line="$(sed -n '1p' "$case_root/signature.sig")"
        second_line="$(sed -n '2p' "$case_root/signature.sig")"
        last_line="$(sed -n '$p' "$case_root/signature.sig")"
        if [[ -n "$first_line" && "$second_line" == "$first_line" && "$last_line" == "$first_line" ]]; then
          tmp_norm="$(mktemp)"
          sed '2d;$d' "$case_root/signature.sig" > "$tmp_norm"
          mv "$tmp_norm" "$case_root/signature.sig"
        fi
      fi
    fi

    if [[ -s "$sigb_file" ]]; then
      cp "$sigb_file" "$case_root/signature_tail_bytes.log"
    fi
    if [[ -s "$ev_file" ]]; then
      cp "$ev_file" "$case_root/events.log"
    fi
  done
} < "$per_case_csv"

# Optionally build golden comparison and add golden_status column into per_case_report.csv.
golden_csv="$out_dir/golden_compare.csv"
if [[ -n "${GOLDEN_ROOT:-}" && -d "${GOLDEN_ROOT:-}" ]]; then
  printf "case,status,golden_sig,local_sig,mismatch_index,expected_value,actual_value\n" > "$golden_csv"

  for d in "$per_case_dir"/*; do
    [[ -d "$d" ]] || continue
    case_name="$(basename "$d")"
    case_name="${case_name#*_}"
    case_name="${case_name%%__attempt*}"
    local_sig="$d/signature.sig"
    case_status="$(awk -F: '/^STATUS[[:space:]]*:/ {gsub(/^[[:space:]]+/, "", $2); print $2; exit}' "$d/case_report.txt" 2>/dev/null || true)"

    if [[ "$case_status" == "SKIPPED" ]]; then
      printf "%s,SKIPPED,,%s,,,\n" "$case_name" "$local_sig" >> "$golden_csv"
      continue
    fi

    golden_sig="$(find "$GOLDEN_ROOT" -type f -name "${case_name}.sig" | head -n1 || true)"

    if [[ -z "$golden_sig" ]]; then
      printf "%s,NO_GOLDEN,,%s,,,\n" "$case_name" "$local_sig" >> "$golden_csv"
    elif diff -u "$golden_sig" "$local_sig" > "$d/signature.diff"; then
      rm -f "$d/signature.diff"
      printf "%s,MATCH,%s,%s,,,\n" "$case_name" "$golden_sig" "$local_sig" >> "$golden_csv"
    else
      mismatch_info="$(
        awk '
          NR==FNR {
            g[FNR]=$0;
            next;
          }
          {
            a[FNR]=$0;
          }
          END {
            max=(length(g) > length(a) ? length(g) : length(a));
            for (i = 1; i <= max; i++) {
              gv=((i in g) ? g[i] : "");
              av=((i in a) ? a[i] : "");
              if (gv != av) {
                printf "%d,%s,%s\n", i - 1, gv, av;
                exit;
              }
            }
          }
        ' "$golden_sig" "$local_sig"
      )"
      mismatch_index=""
      expected_value=""
      actual_value=""
      if [[ -n "$mismatch_info" ]]; then
        IFS=, read -r mismatch_index expected_value actual_value <<< "$mismatch_info"
      fi
      printf "%s,MISMATCH,%s,%s,%s,%s,%s\n" \
        "$case_name" "$golden_sig" "$local_sig" \
        "$mismatch_index" "$expected_value" "$actual_value" >> "$golden_csv"
    fi
  done

  golden_match_cases="$(awk -F, 'NR>1 && $2=="MATCH"{c++} END{print c+0}' "$golden_csv")"
  golden_mismatch_cases="$(awk -F, 'NR>1 && $2=="MISMATCH"{c++} END{print c+0}' "$golden_csv")"
  golden_no_golden_cases="$(awk -F, 'NR>1 && $2=="NO_GOLDEN"{c++} END{print c+0}' "$golden_csv")"
  golden_skipped_cases="$(awk -F, 'NR>1 && $2=="SKIPPED"{c++} END{print c+0}' "$golden_csv")"
else
  golden_match_cases=0
  golden_mismatch_cases=0
  golden_no_golden_cases=0
  golden_skipped_cases=0
fi

# Always add golden_status column to per_case_report.csv.
# If GOLDEN_ROOT was not provided, values are NO_COMPARE.
tmp_per_case_csv="$(mktemp)"
if [[ -f "$golden_csv" ]]; then
  awk -F, '
    BEGIN { OFS="," }
    FNR==NR {
      if (NR>1) {
        gm[$1]=$2;
        gmi[$1]=$5;
        ge[$1]=$6;
        ga[$1]=$7;
      }
      next;
    }
    FNR==1 { print $0, "golden_status", "final_verdict", "mismatch_index", "expected_value", "actual_value"; next; }
    {
      if ($3=="SKIPPED") {
        gs="SKIPPED";
        fv="SKIPPED";
        mi="";
        ev="";
        av="";
      } else {
        gs=(($1 in gm) ? gm[$1] : "NO_COMPARE");
        fv=(($3=="PASS" && gs=="MATCH") ? "PASS" : "FAIL");
        mi=(($1 in gmi) ? gmi[$1] : "");
        ev=(($1 in ge) ? ge[$1] : "");
        av=(($1 in ga) ? ga[$1] : "");
      }
      print $0, gs, fv, mi, ev, av;
    }
  ' "$golden_csv" "$per_case_csv" > "$tmp_per_case_csv"
else
  awk -F, '
    BEGIN { OFS="," }
    NR==1 { print $0, "golden_status", "final_verdict", "mismatch_index", "expected_value", "actual_value"; next; }
    {
      if ($3=="SKIPPED") print $0, "SKIPPED", "SKIPPED", "", "", "";
      else print $0, "NO_COMPARE", "FAIL", "", "", "";
    }
  ' "$per_case_csv" > "$tmp_per_case_csv"
fi
mv "$tmp_per_case_csv" "$per_case_csv"

final_pass_cases="$(awk -F, 'NR>1 && $12=="PASS"{c++} END{print c+0}' "$per_case_csv")"
final_fail_cases="$(awk -F, 'NR>1 && $12=="FAIL"{c++} END{print c+0}' "$per_case_csv")"
final_skipped_cases="$(awk -F, 'NR>1 && $12=="SKIPPED"{c++} END{print c+0}' "$per_case_csv")"

cat >> "$suite_summary_txt" <<EOF
golden_match_cases=$golden_match_cases
golden_mismatch_cases=$golden_mismatch_cases
golden_no_golden_cases=$golden_no_golden_cases
golden_skipped_cases=$golden_skipped_cases
final_pass_cases=$final_pass_cases
final_fail_cases=$final_fail_cases
final_skipped_cases=$final_skipped_cases
EOF

# Emit complete ACT-style suite report.
{
  echo "RVCP-SUITE-REPORT: ACT RUN"
  echo "INPUT_LOG   : $in_log"
  echo "TOTAL_CASES : $total_cases"
  echo "PASS_CASES  : $pass_cases"
  echo "FAIL_CASES  : $fail_cases"
  echo "TIMEOUTS    : $timeout_cases"
  echo "ERRORS      : $error_cases"
  echo "SKIPPED     : $skipped_cases"
  echo "GOLDEN_MATCH_CASES    : $golden_match_cases"
  echo "GOLDEN_MISMATCH_CASES : $golden_mismatch_cases"
  echo "GOLDEN_NO_GOLDEN_CASES: $golden_no_golden_cases"
  echo "GOLDEN_SKIPPED_CASES  : $golden_skipped_cases"
  echo "FINAL_PASS_CASES      : $final_pass_cases"
  echo "FINAL_FAIL_CASES      : $final_fail_cases"
  echo "FINAL_SKIPPED_CASES   : $final_skipped_cases"
  if [[ -f "$golden_csv" ]]; then
    echo "GOLDEN_COMPARE: $golden_csv"
  else
    echo "GOLDEN_COMPARE: disabled (set GOLDEN_ROOT)"
  fi
  echo
  echo "PER-CASE:"
  awk -F, '
    NR==1 { next }
    {
      sw="FAILED";
      if ($3=="PASS") sw="PASSED";
      printf("RVCP-SUMMARY: Test File \"%s.S\": %s\n", $1, sw);
    }
  ' "$per_case_csv"
} > "$act_suite_txt"

echo "Wrote:"
echo "  $summary_txt"
echo "  $suite_summary_txt"
echo "  $per_case_csv"
echo "  $events_log"
echo "  $sig_words"
echo "  $sig64_file"
echo "  $act_suite_txt"
echo "  $per_case_dir"
