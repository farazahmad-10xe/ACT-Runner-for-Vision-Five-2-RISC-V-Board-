#!/usr/bin/env python3
"""Generate cross-build suite and test history pages for the Jenkins site."""

from __future__ import annotations

import csv
import html
import json
import re
from collections import defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Callable
from urllib.parse import quote


PKT = timezone(timedelta(hours=5), name="PKT")


def safe_name(name: str) -> str:
    value = re.sub(r"[^A-Za-z0-9_.-]+", "_", name).strip("._")
    return value or "unnamed"


def status_class(value: str) -> str:
    normalized = value.upper()
    if normalized == "PASS":
        return "pass"
    if normalized in {"FAIL", "FAIL_OR_BLOCKED", "ERROR", "TIMEOUT"}:
        return "fail"
    if normalized in {"SKIP", "SKIPPED", "NOT RUN", "NOT_RUN", "DISABLED"}:
        return "skip"
    return "unknown"


def badge(value: str) -> str:
    shown = value or "NOT RUN"
    return f'<span class="badge {status_class(shown)}">{html.escape(shown)}</span>'


def normalize_status(value: object) -> str:
    if value is None:
        return "NOT RUN"
    shown = str(value).strip()
    return shown if shown else "NOT RUN"


def vf2_outcome(event: dict) -> str:
    status = normalize_status(event.get("vf2")).upper().replace("_", " ")
    category = str(event.get("category", "")).lower()
    if "TIMEOUT" in status or any(token in category for token in ("timeout", "watchdog", "hang")):
        return "TIMEOUT"
    if status == "PASS":
        return "PASS"
    if status in {"FAIL", "FAILED", "ERROR", "FAIL OR BLOCKED"}:
        return "FAIL"
    return "NOT RUN"


def outcome_counts(events: list[dict]) -> dict[str, int]:
    counts = {name: 0 for name in ("PASS", "FAIL", "TIMEOUT", "NOT RUN")}
    for event in events:
        counts[vf2_outcome(event)] += 1
    return counts


def read_status_tsv(path: Path) -> dict[str, str]:
    statuses: dict[str, str] = {}
    if not path.is_file():
        return statuses
    with path.open(newline="", encoding="utf-8", errors="replace") as stream:
        for row in csv.reader(stream, delimiter="\t"):
            if len(row) < 2 or not row[0] or row[0].lower() == "test_name":
                continue
            statuses[row[0]] = row[1] or "UNKNOWN"
    return statuses


def read_cases(path: Path) -> dict[str, dict]:
    cases: dict[str, dict] = {}
    if not path.is_file():
        return cases
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError, TypeError):
        return cases
    if not isinstance(payload, list):
        return cases
    # Later entries win. This deliberately collapses retry duplicates in cases.json.
    for item in payload:
        if not isinstance(item, dict):
            continue
        name = str(item.get("test_name", ""))
        if name:
            cases[name] = item
    return cases


def read_manifest(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.is_file():
        return values
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        key, separator, value = line.partition("=")
        if separator and key:
            values[key.strip()] = value.strip()
    return values


def parse_completed_time(state_root: Path, run_root: Path, manifest: dict[str, str]) -> datetime:
    raw = manifest.get("completed_utc", "")
    if raw:
        try:
            parsed = datetime.fromisoformat(raw.replace("Z", "+00:00"))
            return parsed if parsed.tzinfo else parsed.replace(tzinfo=timezone.utc)
        except ValueError:
            pass
    candidates = (
        run_root / "uart_capture.log",
        run_root / "cases.json",
        state_root / "build_description.txt",
        state_root,
    )
    for candidate in candidates:
        try:
            return datetime.fromtimestamp(candidate.stat().st_mtime, timezone.utc)
        except OSError:
            continue
    return datetime.fromtimestamp(0, timezone.utc)


def display_time(value: datetime) -> str:
    return value.astimezone(PKT).strftime("%Y-%m-%d %H:%M:%S PKT")


def utc_time(value: datetime) -> str:
    return value.astimezone(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")


def run_number(run_id: str, manifest: dict[str, str]) -> int | None:
    raw = manifest.get("build_number", "")
    if raw.isdigit():
        return int(raw)
    match = re.fullmatch(r".+_(\d+)", run_id)
    return int(match.group(1)) if match else None


def source_extension_map(state_root: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    source_root = state_root / "all_priv_tests" / "priv"
    if not source_root.is_dir():
        return result
    for source in source_root.rglob("*.S"):
        if source.name.endswith(".orig.S"):
            continue
        result.setdefault(source.stem, source.parent.name)
    return result


def derive_job_base(build_url: str) -> str:
    cleaned = build_url.rstrip("/")
    match = re.match(r"^(.*)/\d+$", cleaned)
    return match.group(1) if match else ""


def ratio(events: list[dict], target: str) -> tuple[int, int]:
    values = [
        str(event[target])
        for event in events
        if str(event.get(target, "NOT RUN")).upper() not in {"", "NOT RUN", "NOT_RUN"}
    ]
    return sum(value.upper() == "PASS" for value in values), len(values)


def ratio_text(events: list[dict], target: str) -> str:
    passed, total = ratio(events, target)
    return f"{passed} / {total}" if total else "—"


def vf2_pass_total(events: list[dict]) -> str:
    if not events:
        return "—"
    counts = outcome_counts(events)
    return f"{counts['PASS']} / {len(events)}"


def latest_recorded_sail_version(runs: list[dict]) -> str:
    return next(
        (
            str(run["sail_version"])
            for run in reversed(runs)
            if str(run.get("sail_version", "")).lower() not in {"", "unknown"}
        ),
        "unknown",
    )


def status_trend(events: list[dict], target: str, limit: int = 8) -> str:
    recent = list(reversed(events[-limit:]))
    if not recent:
        return "—"
    return '<span class="trend">' + "".join(badge(str(event[target])) for event in recent) + "</span>"


def build_history_dashboard(
    *,
    workspace: Path,
    state_root: Path,
    site_root: Path,
    build_url: str,
    fallback_extension: Callable[[str], str],
    issue_links: Callable[[str], str],
    archived_run_sources: list[tuple[Path, Path]] | None = None,
    report_url_name: str = "Result_20Summary",
) -> dict[str, int]:
    """Build history pages from workspace and retained Jenkins run data."""

    history_root = site_root / "history"
    suites_root = history_root / "suites"
    tests_root = history_root / "tests"
    reports_root = history_root / "reports"
    suites_root.mkdir(parents=True, exist_ok=True)
    tests_root.mkdir(parents=True, exist_ok=True)
    reports_root.mkdir(parents=True, exist_ok=True)

    weekly_root = workspace / "logs" / "jenkins" / "weekly"
    hardware_root = workspace / "logs" / "runs"
    loaded_runs: list[dict] = []
    known_extensions: dict[str, str] = {}

    # Jenkins weekly workspaces are deliberately cleaned before every build.
    # Load archived runs first, then let current workspace paths replace them.
    # This keeps history across clean checkouts without copying large artifacts
    # back into the active workspace.
    run_sources: dict[str, tuple[Path, Path]] = {}
    for historical_state, historical_run in archived_run_sources or []:
        if historical_state.is_dir():
            run_sources[historical_state.name] = (historical_state, historical_run)
    for historical_state in weekly_root.glob("jenkins_weekly_*"):
        if historical_state.is_dir():
            run_sources[historical_state.name] = (
                historical_state,
                hardware_root / historical_state.name,
            )
    if state_root.is_dir():
        run_sources[state_root.name] = (state_root, hardware_root / state_root.name)

    for historical_state, historical_run in sorted(
        run_sources.values(), key=lambda paths: paths[0].name
    ):
        run_id = historical_state.name
        manifest = read_manifest(historical_state / "jenkins_manifest.txt")
        run_id = manifest.get("run_id", run_id)
        sail = read_status_tsv(historical_state / "sail_reference_status.tsv")
        spike = read_status_tsv(historical_state / "spike_status.tsv")
        cases = read_cases(historical_run / "cases.json")
        extensions = source_extension_map(historical_state)
        known_extensions.update(extensions)
        completed = parse_completed_time(historical_state, historical_run, manifest)
        names = set(sail) | set(spike) | set(cases)

        # Empty state directories are not executions and should not appear as runs.
        if not names and not manifest:
            continue
        loaded_runs.append(
            {
                "run_id": run_id,
                "number": run_number(run_id, manifest),
                "state_root": historical_state,
                "run_root": historical_run,
                "sail_version": manifest.get("sail_version", "") or "unknown",
                "sail": sail,
                "spike": spike,
                "cases": cases,
                "extensions": extensions,
                "names": names,
                "completed": completed,
            }
        )

    loaded_runs.sort(key=lambda run: (run["completed"], run["run_id"]))
    job_base = derive_job_base(build_url)

    test_events: dict[str, list[dict]] = defaultdict(list)
    suite_tests: dict[str, set[str]] = defaultdict(set)
    suite_run_events: dict[str, dict[str, list[dict]]] = defaultdict(lambda: defaultdict(list))

    for run in loaded_runs:
        for name in sorted(run["names"], key=str.casefold):
            extension = run["extensions"].get(name) or known_extensions.get(name)
            if not extension:
                extension = fallback_extension(name)
            if not extension:
                extension = "Unclassified"
            known_extensions.setdefault(name, extension)
            case = run["cases"].get(name, {})
            event = {
                "name": name,
                "extension": extension,
                "run": run,
                "sail": run["sail"].get(name, "NOT RUN"),
                "spike": run["spike"].get(name, "NOT RUN"),
                "vf2": normalize_status(case.get("status")) if case else "NOT RUN",
                "category": str(case.get("category", "")),
            }
            test_events[name].append(event)
            suite_tests[extension].add(name)
            suite_run_events[extension][run["run_id"]].append(event)

    def build_page_url(run: dict, suffix: str = "") -> str:
        if not job_base or run["number"] is None:
            return ""
        base = f"{job_base}/{run['number']}"
        return f"{base}/{suffix.lstrip('/')}" if suffix else base + "/"

    def href(url: str, label: str) -> str:
        if not url:
            return ""
        return f'<a href="{html.escape(url, quote=True)}">{html.escape(label)}</a>'

    def run_links(event: dict, local_report: str) -> str:
        run = event["run"]
        links: list[str] = []
        if local_report:
            links.append(f'<a href="{html.escape(local_report, quote=True)}">VF2 report</a>')
        published = build_page_url(
            run,
            f"{report_url_name}/tests/{quote(safe_name(event['name']))}/",
        )
        if published:
            links.append(href(published, "Run details"))
        return " &middot; ".join(link for link in links if link) or "—"

    def write_vf2_report_page(event: dict) -> str:
        run = event["run"]
        case = run["cases"].get(event["name"])
        if not case:
            return ""
        source = run["run_root"] / "per_case" / event["name"] / "report.md"
        if source.is_file():
            report_text = source.read_text(encoding="utf-8", errors="replace")
            source_note = "Collected VF2 report.md"
        else:
            report_text = json.dumps(case, indent=2, sort_keys=True)
            source_note = "VF2 case metadata (report.md was not retained)"
        report_dir = reports_root / safe_name(run["run_id"]) / safe_name(event["name"])
        report_dir.mkdir(parents=True, exist_ok=True)
        report_html = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(event["name"])} VF2 report</title><link rel="stylesheet" href="../../../../style.css"></head>
<body><main><p><a href="../../../tests/{quote(safe_name(event["name"]))}/index.html">&larr; Test history</a></p>
<h1>{html.escape(event["name"])} VF2 Report</h1>
<p class="subtitle">Run <strong>{html.escape(run["run_id"])}</strong> &middot;
Completed {html.escape(display_time(run["completed"]))} &middot;
Sail version: <strong>{html.escape(run["sail_version"])}</strong> &middot;
{html.escape(source_note)}</p>
<div class="status-grid">
<div><strong>Sail</strong>{badge(event["sail"])}</div>
<div><strong>Spike</strong>{badge(event["spike"])}</div>
<div><strong>VF2</strong>{badge(event["vf2"])}</div></div>
<h2>Reported ACT/Sail tickets</h2>
<p>{issue_links(event["name"]) or 'No GitHub ticket is recorded for this test in the tracking sheet.'}</p>
<pre>{html.escape(report_text)}</pre>
</main></body></html>"""
        (report_dir / "index.html").write_text(report_html, encoding="utf-8")
        return (
            "../../reports/"
            f"{quote(safe_name(run['run_id']))}/{quote(safe_name(event['name']))}/index.html"
        )

    # Per-test execution histories.
    for name, events in sorted(test_events.items(), key=lambda item: item[0].casefold()):
        newest_first = list(reversed(events))
        extension = newest_first[0]["extension"]
        latest_sail_version = latest_recorded_sail_version(
            [event["run"] for event in events]
        )
        history_rows: list[str] = []
        for event in newest_first:
            run = event["run"]
            local_report = write_vf2_report_page(event)
            history_rows.append(
                "<tr>"
                f'<td class="nowrap" title="{html.escape(utc_time(run["completed"]), quote=True)}">'
                f'{html.escape(display_time(run["completed"]))}</td>'
                f"<td>{html.escape(run['run_id'])}</td>"
                f"<td>{html.escape(run['sail_version'])}</td>"
                f"<td>{badge(event['sail'])}</td>"
                f"<td>{badge(event['spike'])}</td>"
                f"<td>{badge(event['vf2'])}</td>"
                f"<td>{html.escape(event['category'] or '—')}</td>"
                f"<td>{run_links(event, local_report)}</td>"
                "</tr>"
            )
        test_dir = tests_root / safe_name(name)
        test_dir.mkdir(parents=True, exist_ok=True)
        latest_detail = site_root / "tests" / safe_name(name) / "index.html"
        latest_detail_link = (
            f' &middot; <a href="../../../tests/{quote(safe_name(name))}/index.html">Latest-run details</a>'
            if latest_detail.is_file()
            else ""
        )
        test_html = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(name)} execution history</title><link rel="stylesheet" href="../../../style.css"></head>
<body><main><p><a href="../../index.html">&larr; History dashboard</a> &middot;
<a href="../../suites/{quote(safe_name(extension))}/index.html">{html.escape(extension)} history</a>
{latest_detail_link}</p>
<h1>{html.escape(name)} Test History</h1>
<p class="subtitle">Extension: <strong>{html.escape(extension)}</strong> &middot;
Latest recorded Sail version: <strong>{html.escape(latest_sail_version)}</strong> &middot;
{len(events)} retained execution record{'s' if len(events) != 1 else ''}. Time is shown in Pakistan Standard Time; hover for UTC.</p>
<div class="metrics">
<div class="metric">Sail history<strong>{ratio_text(events, 'sail')} PASS</strong></div>
<div class="metric">Spike history<strong>{ratio_text(events, 'spike')} PASS</strong></div>
<div class="metric">VF2 history<strong>{ratio_text(events, 'vf2')} PASS</strong></div></div>
<h2>Reported ACT/Sail tickets</h2>
<p>{issue_links(name) or 'No GitHub ticket is recorded for this test in the tracking sheet.'}</p>
<table><thead><tr><th>Completed</th><th>Jenkins run</th><th>Sail version</th><th>Sail</th><th>Spike</th><th>VF2</th><th>Category</th><th>Evidence</th></tr></thead>
<tbody>{''.join(history_rows)}</tbody></table>
</main></body></html>"""
        (test_dir / "index.html").write_text(test_html, encoding="utf-8")

    # Per-suite dashboards, including a run timeline and the current status of each test.
    suite_index_rows: list[str] = []
    for extension in sorted(suite_tests, key=str.casefold):
        suite_run_map = suite_run_events[extension]
        ordered_suite_runs = [
            run for run in loaded_runs if run["run_id"] in suite_run_map
        ]
        latest_run = ordered_suite_runs[-1] if ordered_suite_runs else None
        latest_vf2_run = next(
            (
                run
                for run in reversed(ordered_suite_runs)
                if any(
                    vf2_outcome(event) != "NOT RUN"
                    for event in suite_run_map[run["run_id"]]
                )
            ),
            None,
        )
        suite_timeline_rows: list[str] = []
        for run in reversed(ordered_suite_runs):
            events = suite_run_map[run["run_id"]]
            vf2_counts = outcome_counts(events)
            run_summary = build_page_url(run, f"{report_url_name}/")
            run_label = href(run_summary, run["run_id"]) if run_summary else html.escape(run["run_id"])
            suite_timeline_rows.append(
                "<tr>"
                f'<td class="nowrap" title="{html.escape(utc_time(run["completed"]), quote=True)}">'
                f'{html.escape(display_time(run["completed"]))}</td>'
                f"<td>{run_label}</td>"
                f"<td>{html.escape(run['sail_version'])}</td>"
                f"<td>{len(events)}</td>"
                f"<td><strong>{ratio_text(events, 'sail')}</strong></td>"
                f"<td><strong>{ratio_text(events, 'spike')}</strong></td>"
                f"<td>{vf2_counts['PASS']}</td>"
                f"<td>{vf2_counts['FAIL']}</td>"
                f"<td>{vf2_counts['TIMEOUT']}</td>"
                f"<td>{vf2_counts['NOT RUN']}</td>"
                "</tr>"
            )

        suite_test_rows: list[str] = []
        for name in sorted(suite_tests[extension], key=str.casefold):
            events = test_events[name]
            latest = events[-1]
            test_sail_version = latest_recorded_sail_version(
                [event["run"] for event in events]
            )
            suite_test_rows.append(
                "<tr>"
                f'<td><a href="../../tests/{quote(safe_name(name))}/index.html">{html.escape(name)}</a></td>'
                f"<td>{html.escape(test_sail_version)}</td>"
                f"<td>{badge(latest['sail'])}</td>"
                f"<td>{badge(latest['spike'])}</td>"
                f"<td>{badge(latest['vf2'])}</td>"
                f"<td>{len(events)}</td>"
                f"<td>{status_trend(events, 'vf2')}</td>"
                f"<td>{issue_links(name) or '—'}</td>"
                "</tr>"
            )

        suite_dir = suites_root / safe_name(extension)
        suite_dir.mkdir(parents=True, exist_ok=True)
        latest_display = display_time(latest_run["completed"]) if latest_run else "No retained run"
        latest_sail_version = latest_recorded_sail_version(ordered_suite_runs)
        latest_extension = site_root / "extensions" / safe_name(extension) / "index.html"
        latest_extension_link = (
            f' &middot; <a href="../../../extensions/{quote(safe_name(extension))}/index.html">'
            "Latest-run extension results</a>"
            if latest_extension.is_file()
            else ""
        )
        suite_html = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(extension)} execution history</title><link rel="stylesheet" href="../../../style.css"></head>
<body><main><p><a href="../../index.html">&larr; History dashboard</a>
{latest_extension_link}</p>
<h1>{html.escape(extension)} Suite History</h1>
<p class="subtitle">{len(suite_tests[extension])} tracked tests &middot;
{len(ordered_suite_runs)} retained runs &middot;
Latest recorded Sail version: <strong>{html.escape(latest_sail_version)}</strong> &middot;
Latest completion: {html.escape(latest_display)}</p>
<h2>Suite results by execution</h2>
<table><thead><tr><th>Completed</th><th>Jenkins run</th><th>Sail version</th><th>Tests present</th><th>Sail PASS / Total</th><th>Spike PASS / Total</th><th>VF2 PASS</th><th>VF2 FAIL</th><th>VF2 TIMEOUT</th><th>VF2 NOT RUN</th></tr></thead>
<tbody>{''.join(suite_timeline_rows)}</tbody></table>
<h2>Tests in this suite</h2>
<p class="muted">The VF2 trend is newest first and shows up to eight retained executions.</p>
<table><thead><tr><th>Test</th><th>Latest Sail version</th><th>Latest Sail</th><th>Latest Spike</th><th>Latest VF2</th><th>Executions</th><th>VF2 trend</th><th>ACT/Sail tickets</th></tr></thead>
<tbody>{''.join(suite_test_rows)}</tbody></table>
</main></body></html>"""
        (suite_dir / "index.html").write_text(suite_html, encoding="utf-8")

        latest_events = suite_run_map[latest_run["run_id"]] if latest_run else []
        latest_vf2_events = (
            suite_run_map[latest_vf2_run["run_id"]] if latest_vf2_run else []
        )
        latest_vf2_title = (
            f' title="{html.escape(latest_vf2_run["run_id"], quote=True)}"'
            if latest_vf2_run
            else ""
        )
        suite_index_rows.append(
            "<tr>"
            f'<td><a href="suites/{quote(safe_name(extension))}/index.html">{html.escape(extension)}</a></td>'
            f"<td>{len(suite_tests[extension])}</td>"
            f"<td>{len(ordered_suite_runs)}</td>"
            f"<td>{html.escape(latest_display)}</td>"
            f"<td>{html.escape(latest_sail_version)}</td>"
            f"<td><strong>{ratio_text(latest_events, 'sail')}</strong></td>"
            f"<td><strong>{ratio_text(latest_events, 'spike')}</strong></td>"
            f"<td{latest_vf2_title}><strong>{vf2_pass_total(latest_vf2_events)}</strong></td>"
            "</tr>"
        )

    # Overall history dashboard.
    run_rows: list[str] = []
    for run in reversed(loaded_runs):
        events = [
            event
            for name_events in test_events.values()
            for event in name_events
            if event["run"]["run_id"] == run["run_id"]
        ]
        vf2_counts = outcome_counts(events)
        run_summary = build_page_url(run, f"{report_url_name}/")
        run_label = href(run_summary, run["run_id"]) if run_summary else html.escape(run["run_id"])
        run_rows.append(
            "<tr>"
            f'<td class="nowrap" title="{html.escape(utc_time(run["completed"]), quote=True)}">'
            f'{html.escape(display_time(run["completed"]))}</td>'
            f"<td>{run_label}</td>"
            f"<td>{html.escape(run['sail_version'])}</td>"
            f"<td>{len(events)}</td>"
            f"<td><strong>{ratio_text(events, 'sail')}</strong></td>"
            f"<td><strong>{ratio_text(events, 'spike')}</strong></td>"
            f"<td>{vf2_counts['PASS']}</td>"
            f"<td>{vf2_counts['FAIL']}</td>"
            f"<td>{vf2_counts['TIMEOUT']}</td>"
            f"<td>{vf2_counts['NOT RUN']}</td>"
            "</tr>"
        )

    latest_time = display_time(loaded_runs[-1]["completed"]) if loaded_runs else "No retained runs"
    history_html = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>VF2 privileged-test execution history</title><link rel="stylesheet" href="../style.css"></head>
<body><main><p><a href="../index.html">&larr; Current Result Summary</a></p>
<h1>VF2 Privileged-Test Execution History</h1>
<p class="subtitle">Historical Sail, Spike, and VF2 results assembled from retained Jenkins runs.
Completion times are shown in Pakistan Standard Time; hover for UTC.</p>
<div class="metrics">
<div class="metric">Retained runs<strong>{len(loaded_runs)}</strong></div>
<div class="metric">Tracked suites<strong>{len(suite_tests)}</strong></div>
<div class="metric">Tracked tests<strong>{len(test_events)}</strong></div>
<div class="metric">Latest completion<strong>{html.escape(latest_time)}</strong></div></div>
<h2>Jenkins execution timeline</h2>
<p class="muted">VF2 outcomes are grouped explicitly as PASS, FAIL, TIMEOUT, or NOT RUN for every Jenkins execution.</p>
<table><thead><tr><th>Completed</th><th>Jenkins run</th><th>Sail version</th><th>Tests present</th><th>Sail PASS / Total</th><th>Spike PASS / Total</th><th>VF2 PASS</th><th>VF2 FAIL</th><th>VF2 TIMEOUT</th><th>VF2 NOT RUN</th></tr></thead>
<tbody>{''.join(run_rows) if run_rows else '<tr><td colspan="10">No historical runs were found.</td></tr>'}</tbody></table>
<h2>Suites and extensions</h2>
<p>Select a suite to see its execution timeline and the history of every test it contains.
The latest VF2 value is the number of passing tests divided by all tests present in that suite's newest retained hardware execution.</p>
<table><thead><tr><th>Suite / Extension</th><th>Tracked tests</th><th>Runs</th><th>Latest completion</th><th>Latest recorded Sail version</th><th>Latest Sail PASS / Total</th><th>Latest Spike PASS / Total</th><th>Latest VF2 PASS / Total</th></tr></thead>
<tbody>{''.join(suite_index_rows) if suite_index_rows else '<tr><td colspan="8">No historical suites were found.</td></tr>'}</tbody></table>
</main></body></html>"""
    (history_root / "index.html").write_text(history_html, encoding="utf-8")

    return {
        "runs": len(loaded_runs),
        "suites": len(suite_tests),
        "tests": len(test_events),
    }
