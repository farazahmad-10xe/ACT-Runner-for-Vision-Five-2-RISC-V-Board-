#!/usr/bin/env python3
"""Build a persistent cross-job VF2 validation portal from Jenkins records."""

from __future__ import annotations

import argparse
import csv
import html
import json
import os
import re
import shutil
import tempfile
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import quote


JOB_TYPES = {
    "vf2-privileged-sanity": ("Sanity", "sanity", "jenkins_sanity_", "Sanity_20Result_20Summary"),
    "vf2-privileged-weekly": ("Weekly", "weekly", "jenkins_weekly_", "Result_20Summary"),
    "vf2-act-update-validation": ("ACT Update", "act-update", "act_update_", ""),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--jenkins-home", type=Path, required=True)
    parser.add_argument("--data-root", type=Path, required=True)
    parser.add_argument("--site-root", type=Path, required=True)
    parser.add_argument("--jenkins-url", required=True)
    return parser.parse_args()


def child_text(node: ET.Element, name: str, default: str = "") -> str:
    found = node.find(name)
    return found.text.strip() if found is not None and found.text else default


def atomic_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(payload, stream, indent=2, sort_keys=True)
            stream.write("\n")
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def read_json(path: Path, default: object) -> object:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, TypeError):
        return default


def read_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.is_file():
        return values
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        key, separator, value = line.partition("=")
        if not separator:
            key, separator, value = line.partition(":")
            key = re.sub(r"\s+", "_", key.strip().lower())
        if separator:
            values[key.strip()] = value.strip()
    return values


def read_tsv(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    if not path.is_file():
        return result
    with path.open(newline="", encoding="utf-8", errors="replace") as stream:
        for row in csv.reader(stream, delimiter="\t"):
            if len(row) >= 2 and row[0] and row[0].lower() != "test_name":
                result[row[0]] = row[1] or "NOT RUN"
    return result


def normalize(value: object, default: str = "NOT RUN") -> str:
    shown = str(value or "").strip().upper().replace("_", " ")
    return shown or default


def suite_name(name: str) -> str:
    match = re.match(r"^(.*)-\d+$", name)
    if match:
        return match.group(1)
    parts = name.split("_")
    return "_".join(parts[:2]) if len(parts) > 1 else name


def build_cause(root: ET.Element) -> str:
    for node in root.iter():
        if node.tag.endswith("UserIdCause"):
            return f"Manual ({child_text(node, 'userId', 'user')})"
        if node.tag.endswith("TimerTriggerCause"):
            return "Timer"
        if node.tag.endswith("UpstreamCause"):
            return f"Upstream ({child_text(node, 'upstreamProject', 'job')} #{child_text(node, 'upstreamBuild', '?')})"
        if node.tag.endswith("RemoteCause"):
            return "Remote trigger"
    return "Unknown"


def parameters(root: ET.Element) -> dict[str, str]:
    result: dict[str, str] = {}
    for action in root.iter():
        if action.tag.endswith("ParametersAction"):
            for parameter in action.iter():
                name = child_text(parameter, "name")
                if name:
                    result[name] = child_text(parameter, "value")
    return result


def parse_cases(path: Path, state: Path, build_url: str, report: str) -> list[dict]:
    payload = read_json(path, []) if path.is_file() else []
    if not isinstance(payload, list):
        return []
    sail = read_tsv(state / "sail_reference_status.tsv")
    spike = read_tsv(state / "spike_status.tsv")
    cases: dict[str, dict] = {}
    for item in payload:
        if not isinstance(item, dict) or not item.get("test_name"):
            continue
        name = str(item["test_name"])
        report_url = f"{build_url}{report}/downloads/per_case/{quote(name)}/report.md" if report else ""
        cases[name] = {
            "name": name,
            "suite": suite_name(name),
            "sail": normalize(sail.get(name, item.get("sail_status"))),
            "spike": normalize(spike.get(name)),
            "vf2": normalize(item.get("status")),
            "category": str(item.get("category", "")),
            "root_cause": str(item.get("root_cause", "")),
            "report_url": report_url,
        }
    return sorted(cases.values(), key=lambda case: case["name"].lower())


def test_counts(tests: list[dict]) -> dict[str, int]:
    values = [normalize(test.get("vf2")) for test in tests]
    failures = {"FAIL", "FAILED", "ERROR", "TIMEOUT"}
    return {
        "total": len(values),
        "pass": sum(value == "PASS" for value in values),
        "fail": sum(value in failures for value in values),
        "not_run": sum(value != "PASS" and value not in failures for value in values),
    }


def parse_build(job: str, directory: Path, base_url: str, existing: dict) -> dict | None:
    try:
        root = ET.parse(directory / "build.xml").getroot()
    except (OSError, ET.ParseError):
        return None
    label, kind, prefix, report = JOB_TYPES[job]
    number = int(directory.name)
    run_id = f"{prefix}{number}"
    build_url = f"{base_url}/job/{quote(job)}/{number}/"
    archive = directory / "archive"
    state = archive / "logs" / "jenkins" / kind / run_id
    run = archive / "logs" / "runs" / run_id
    manifest_file = "update_manifest.txt" if kind == "act-update" else "jenkins_manifest.txt"
    manifest = read_values(state / manifest_file)
    runner = read_values(state / "runner_resolution.txt")
    act = read_values(state / "act_resolution.txt")
    tests = parse_cases(run / "cases.json", state, build_url, report)
    if not tests and isinstance(existing.get("tests"), list):
        tests = existing["tests"]
    counts = test_counts(tests)
    sail_counts = read_values(state / "sail_counts.txt")
    if kind == "act-update" and not tests:
        counts["total"] = int(sail_counts.get("results_count", "0") or 0)
    values = parameters(root)
    timestamp_ms = int(child_text(root, "timestamp", "0") or 0)
    started = datetime.fromtimestamp(timestamp_ms / 1000, timezone.utc)
    status = normalize(child_text(root, "result"), "RUNNING")
    report_url = f"{build_url}{report}/" if report and (directory / "htmlreports").is_dir() else ""
    return {
        "schema": 1,
        "job": job,
        "job_label": label,
        "build": number,
        "run_id": run_id,
        "status": status,
        "cause": build_cause(root),
        "started_utc": started.isoformat().replace("+00:00", "Z"),
        "duration_seconds": round(int(child_text(root, "duration", "0") or 0) / 1000),
        "indexed_utc": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "sail_version": manifest.get("sail_version", existing.get("sail_version", "unknown")) or "unknown",
        "runner_branch": values.get("RUNNER_BRANCH", runner.get("runner_branch", "")),
        "runner_revision": runner.get("selected_runner_sha", manifest.get("git_head", existing.get("runner_revision", ""))),
        "act_branch": values.get("ACT_BRANCH", act.get("act_branch", manifest.get("official_branch", ""))),
        "act_revision": act.get("selected_act_sha", manifest.get("act_git_head", manifest.get("official_revision", existing.get("act_revision", "")))),
        "platform": "vf2_u74" if kind != "act-update" else "ACT/Sail",
        "counts": counts,
        "tests": tests,
        "urls": {
            "build": build_url,
            "console": f"{build_url}console",
            "artifacts": f"{build_url}artifact/" if archive.is_dir() else "",
            "dashboard": report_url,
        },
    }


def scan(jenkins_home: Path, data_root: Path, base_url: str) -> int:
    indexed = 0
    for job in JOB_TYPES:
        builds = jenkins_home / "jobs" / job / "builds"
        if not builds.is_dir():
            continue
        for directory in builds.iterdir():
            if not directory.name.isdigit() or not (directory / "build.xml").is_file():
                continue
            destination = data_root / "records" / job / f"{directory.name}.json"
            old = read_json(destination, {})
            existing = old if isinstance(old, dict) else {}
            # Completed Jenkins builds are immutable. Retain their compact
            # normalized record and only revisit new or still-running builds.
            if destination.is_file() and existing.get("status") != "RUNNING":
                continue
            record = parse_build(job, directory, base_url, existing)
            if record:
                atomic_json(destination, record)
                indexed += 1
    return indexed


def load_records(data_root: Path) -> list[dict]:
    records: list[dict] = []
    for path in (data_root / "records").glob("*/*.json"):
        record = read_json(path, {})
        if isinstance(record, dict) and record.get("job") and record.get("build") is not None:
            records.append(record)
    return sorted(records, key=lambda item: item.get("started_utc", ""), reverse=True)


def status_badge(value: str) -> str:
    shown = normalize(value)
    css = "pass" if shown in {"PASS", "SUCCESS"} else "fail" if shown in {"FAIL", "FAILED", "FAILURE", "ERROR", "TIMEOUT"} else "warn"
    return f'<span class="badge {css}">{html.escape(shown)}</span>'


def anchor(url: str, label: str) -> str:
    return f'<a href="{html.escape(url, quote=True)}">{html.escape(label)}</a>' if url else "—"


def short_sha(value: str) -> str:
    return value[:10] if value else "—"


def display_time(value: str) -> str:
    return re.sub(r"\.\d+(?=Z$)", "", value).replace("T", " ").replace("Z", " UTC")


def build_site(site: Path, records: list[dict]) -> None:
    if site.exists():
        shutil.rmtree(site)
    site.mkdir(parents=True)
    generated = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    latest: dict[str, dict] = {}
    for record in records:
        latest.setdefault(record["job"], record)
    cards: list[str] = []
    for job, (label, _kind, _prefix, _report) in JOB_TYPES.items():
        record = latest.get(job)
        if not record:
            cards.append(f'<article class="card"><h2>{html.escape(label)}</h2><p>No indexed builds</p></article>')
            continue
        counts = record.get("counts", {})
        target = record["urls"].get("dashboard") or record["urls"].get("build")
        cards.append(
            f'<article class="card"><h2>{html.escape(label)}</h2><p>{status_badge(record.get("status"))} Build #{record["build"]}</p>'
            f'<p>{counts.get("pass", 0)} PASS / {counts.get("fail", 0)} FAIL / {counts.get("total", 0)} total</p>'
            f'<p>Sail {html.escape(record.get("sail_version", "unknown"))}</p><p>{anchor(target, "Open latest result")}</p></article>'
        )
    run_rows: list[str] = []
    test_rows: list[str] = []
    for record in records:
        counts = record.get("counts", {})
        search = f'{record["job_label"]} {record["build"]}'.lower()
        run_rows.append(
            f'<tr data-job="{html.escape(record["job_label"], quote=True)}" data-status="{html.escape(normalize(record.get("status")), quote=True)}" data-sail="{html.escape(record.get("sail_version", "unknown"), quote=True)}" data-search="{html.escape(search, quote=True)}">'
            f'<td>{html.escape(record["job_label"])}</td><td>{anchor(record["urls"].get("build", ""), "#" + str(record["build"]))}</td><td>{status_badge(record.get("status"))}</td>'
            f'<td>{html.escape(display_time(record.get("started_utc", "")))}</td><td>{html.escape(record.get("cause", ""))}</td>'
            f'<td>{record.get("duration_seconds", 0) // 60} min</td><td>{html.escape(record.get("sail_version", "unknown"))}</td>'
            f'<td><code>{html.escape(short_sha(record.get("runner_revision", "")))}</code></td><td><code>{html.escape(short_sha(record.get("act_revision", "")))}</code></td>'
            f'<td>{counts.get("pass", 0)} / {counts.get("fail", 0)} / {counts.get("total", 0)}</td>'
            f'<td>{anchor(record["urls"].get("dashboard", ""), "Dashboard")} · {anchor(record["urls"].get("artifacts", ""), "Artifacts")} · {anchor(record["urls"].get("console", ""), "Console")}</td></tr>'
        )
        for test in record.get("tests", []):
            test_search = " ".join((test.get("name", ""), test.get("suite", ""), test.get("category", ""))).lower()
            test_rows.append(
                f'<tr data-job="{html.escape(record["job_label"], quote=True)}" data-status="{html.escape(normalize(test.get("vf2")), quote=True)}" data-sail="{html.escape(normalize(test.get("sail")), quote=True)}" data-search="{html.escape(test_search, quote=True)}">'
                f'<td>{html.escape(test.get("name", ""))}</td><td>{html.escape(test.get("suite", ""))}</td><td>{html.escape(record["job_label"])} #{record["build"]}</td>'
                f'<td>{status_badge(test.get("sail"))}</td><td>{status_badge(test.get("spike"))}</td><td>{status_badge(test.get("vf2"))}</td>'
                f'<td>{html.escape(test.get("category", ""))}</td><td>{anchor(test.get("report_url", ""), "Report")}</td></tr>'
            )
    atomic_json(site / "data.json", {"generated_utc": generated, "records": records})
    (site / "style.css").write_text(CSS.strip() + "\n", encoding="utf-8")
    (site / "app.js").write_text(JS.strip() + "\n", encoding="utf-8")
    options = "".join(f"<option>{html.escape(item[0])}</option>" for item in JOB_TYPES.values())
    page = PAGE.format(
        generated=html.escape(generated), cards="".join(cards), options=options,
        run_count=len(run_rows), run_rows="".join(run_rows), test_count=len(test_rows), test_rows="".join(test_rows),
    )
    (site / "index.html").write_text(page, encoding="utf-8")


CSS = """
:root{font-family:Inter,system-ui,sans-serif;color:#172033;background:#f4f7fb}body{margin:0}main{max-width:1700px;margin:auto;padding:24px}.muted{color:#64748b}.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:14px;margin:22px 0}.card,.panel{background:white;border:1px solid #dbe3ee;border-radius:10px;padding:16px;margin-bottom:18px;box-shadow:0 2px 8px #1e293b0b}.filters{display:flex;gap:10px;flex-wrap:wrap;margin:12px 0}.filters input,.filters select{padding:8px;border:1px solid #b8c4d4;border-radius:6px;background:white}.table-wrap{overflow:auto;max-height:650px}table{width:100%;border-collapse:collapse;font-size:.88rem}th,td{padding:8px 9px;border-bottom:1px solid #e5eaf1;text-align:left;vertical-align:top}th{position:sticky;top:0;background:#eaf0f8;z-index:1}.badge{display:inline-block;padding:2px 8px;border-radius:999px;font-weight:700;font-size:.75rem;white-space:nowrap}.pass{background:#d1fae5;color:#065f46}.fail{background:#fee2e2;color:#991b1b}.warn{background:#fef3c7;color:#92400e}code{font-size:.78rem}a{color:#0369a1}.hidden{display:none}.count{font-weight:700}
"""

JS = """
function applyFilters(section){const root=document.querySelector(`[data-section="${section}"]`);const job=root.querySelector('[data-filter="job"]').value;const status=root.querySelector('[data-filter="status"]').value;const sail=root.querySelector('[data-filter="sail"]').value;const search=root.querySelector('[data-filter="search"]').value.toLowerCase();let shown=0;root.querySelectorAll('tbody tr').forEach(row=>{const ok=(!job||row.dataset.job===job)&&(!status||row.dataset.status===status)&&(!sail||row.dataset.sail===sail)&&(!search||row.dataset.search.includes(search));row.classList.toggle('hidden',!ok);if(ok)shown++;});root.querySelector('.count').textContent=shown;}
document.querySelectorAll('[data-section]').forEach(root=>root.querySelectorAll('input,select').forEach(input=>input.addEventListener('input',()=>applyFilters(root.dataset.section))));
"""

PAGE = """<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>VF2 validation portal</title><link rel="stylesheet" href="style.css"></head><body><main>
<h1>VF2 validation portal</h1><p class="muted">Sanity, weekly and ACT-update status · generated {generated}</p><div class="cards">{cards}</div>
<section class="panel" data-section="runs"><h2>Job runs</h2><div class="filters"><select data-filter="job"><option value="">All jobs</option>{options}</select><select data-filter="status"><option value="">All statuses</option><option>SUCCESS</option><option>UNSTABLE</option><option>FAILURE</option><option>ABORTED</option><option>RUNNING</option></select><select data-filter="sail"><option value="">All Sail versions</option><option>0.13</option><option>unknown</option></select><input data-filter="search" placeholder="Build or job"><span>Showing <span class="count">{run_count}</span></span></div>
<div class="table-wrap"><table><thead><tr><th>Job</th><th>Build</th><th>Status</th><th>Started</th><th>Trigger</th><th>Duration</th><th>Sail</th><th>Runner SHA</th><th>ACT SHA</th><th>VF2 P/F/T</th><th>Evidence</th></tr></thead><tbody>{run_rows}</tbody></table></div></section>
<section class="panel" data-section="tests"><h2>Test-case results</h2><div class="filters"><select data-filter="job"><option value="">All jobs</option>{options}</select><select data-filter="status"><option value="">All VF2 statuses</option><option>PASS</option><option>FAIL</option><option>NOT RUN</option><option>TIMEOUT</option></select><select data-filter="sail"><option value="">All Sail statuses</option><option>PASS</option><option>FAIL</option><option>NOT RUN</option></select><input data-filter="search" placeholder="Test, suite or category"><span>Showing <span class="count">{test_count}</span></span></div>
<div class="table-wrap"><table><thead><tr><th>Test</th><th>Suite</th><th>Run</th><th>Sail</th><th>Spike</th><th>VF2</th><th>Category</th><th>Evidence</th></tr></thead><tbody>{test_rows}</tbody></table></div></section><script src="app.js"></script></main></body></html>"""


def main() -> int:
    args = parse_args()
    indexed = scan(args.jenkins_home.resolve(), args.data_root.resolve(), args.jenkins_url.rstrip("/"))
    records = load_records(args.data_root.resolve())
    build_site(args.site_root.resolve(), records)
    print(f"Indexed {indexed} retained Jenkins builds; persistent records: {len(records)}")
    print(f"Portal: {args.site_root.resolve() / 'index.html'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
