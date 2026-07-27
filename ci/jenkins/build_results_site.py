#!/usr/bin/env python3
"""Build a static Jenkins dashboard for one weekly privileged-test run."""

from __future__ import annotations

import argparse
import csv
import html
import json
import re
import shutil
from collections import Counter, defaultdict
from pathlib import Path
from urllib.parse import quote

from build_history_dashboard import build_history_dashboard, read_manifest


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--state-root", type=Path, required=True)
    parser.add_argument("--run-root", type=Path, required=True)
    parser.add_argument("--artifact-root", type=Path, required=True)
    parser.add_argument("--build-url", default="")
    return parser.parse_args()


def read_tsv(path: Path) -> list[list[str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8", errors="replace") as stream:
        return list(csv.reader(stream, delimiter="\t"))


def safe_name(name: str) -> str:
    value = re.sub(r"[^A-Za-z0-9_.-]+", "_", name).strip("._")
    return value or "unnamed"


def status_class(value: str) -> str:
    normalized = value.upper()
    if normalized in {"PASS", "MATCH"}:
        return "pass"
    if normalized in {"FAIL", "FAIL_OR_BLOCKED", "ERROR", "TIMEOUT", "DIFFERENT"}:
        return "fail"
    if normalized in {"SKIP", "SKIPPED", "NOT RUN", "NOT_RUN", "DISABLED"} or normalized.startswith("NO DIRECT"):
        return "skip"
    if normalized in {"OPEN", "CLOSED"}:
        return normalized.lower()
    return "unknown"


def badge(value: str) -> str:
    shown = value or "NOT RUN"
    return f'<span class="badge {status_class(shown)}">{html.escape(shown)}</span>'


def main() -> int:
    args = parse_args()
    workspace = args.workspace.resolve()
    state_root = args.state_root.resolve()
    run_root = args.run_root.resolve()
    artifact_root = args.artifact_root.resolve()
    site_root = state_root / "site"
    site_root.mkdir(parents=True, exist_ok=True)
    (site_root / "tests").mkdir(exist_ok=True)
    (site_root / "extensions").mkdir(exist_ok=True)
    downloads_root = site_root / "downloads"
    downloads_root.mkdir(exist_ok=True)

    build_url = args.build_url.rstrip("/")
    report_url = f"{build_url}/Result_20Summary" if build_url else ""
    manifest = read_manifest(state_root / "jenkins_manifest.txt")
    sail_version = manifest.get("sail_version", "") or "unknown"

    def published_url(relative: Path) -> str:
        encoded = "/".join(quote(part) for part in relative.parts)
        return f"{report_url}/{encoded}" if report_url else encoded

    def publish_file(
        source: Path,
        relative: Path,
        label: str,
        *,
        force_download: bool = False,
        allow_missing_source: bool = False,
    ) -> str:
        destination = site_root / relative
        if source.is_file():
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
        elif not allow_missing_source:
            return ""
        download_attribute = " download" if force_download else ""
        return (
            f'<a href="{html.escape(published_url(relative), quote=True)}"'
            f"{download_attribute}>{html.escape(label)}</a>"
        )

    issue_catalog: dict = {"tickets": {}, "tests": {}}
    issue_catalog_path = Path(__file__).with_name("test_issue_links.json")
    if issue_catalog_path.is_file():
        try:
            loaded_catalog = json.loads(issue_catalog_path.read_text(encoding="utf-8"))
            if isinstance(loaded_catalog, dict):
                issue_catalog = loaded_catalog
        except (json.JSONDecodeError, OSError, TypeError):
            pass

    def issue_links_for_test(name: str) -> str:
        links: list[str] = []
        tickets = issue_catalog.get("tickets", {})
        for key in issue_catalog.get("tests", {}).get(name, []):
            ticket = tickets.get(key, {})
            url = str(ticket.get("url", ""))
            if not url:
                continue
            tracker = str(ticket.get("tracker", "GitHub"))
            number = str(ticket.get("number", ""))
            kind = str(ticket.get("kind", "issue"))
            state = str(ticket.get("state", "unknown")).upper()
            title = str(ticket.get("title", ""))
            label = f"{tracker}{' PR' if kind == 'pull request' else ''} #{number}"
            tooltip = f"{title} [{state}]" if title else state
            links.append(
                f'<a class="ticket {html.escape(state.lower())}" '
                f'href="{html.escape(url, quote=True)}" '
                f'target="_blank" rel="noopener noreferrer" '
                f'title="{html.escape(tooltip, quote=True)}">'
                f'{html.escape(label)} <small>{html.escape(state)}</small></a>'
            )
        return " ".join(links)

    sail_status: dict[str, str] = {}
    sail_rows = read_tsv(state_root / "sail_reference_status.tsv")
    for row in sail_rows[1:] if sail_rows else []:
        if len(row) >= 2 and row[0]:
            sail_status[row[0]] = row[1]

    spike_status: dict[str, str] = {}
    for row in read_tsv(state_root / "spike_status.tsv"):
        if len(row) >= 2 and row[0]:
            spike_status[row[0]] = row[1]

    cases: dict[str, dict] = {}
    cases_json = run_root / "cases.json"
    if cases_json.is_file():
        try:
            for item in json.loads(cases_json.read_text(encoding="utf-8")):
                name = str(item.get("test_name", ""))
                if name:
                    cases[name] = item
        except (json.JSONDecodeError, OSError, TypeError):
            pass

    names = sorted(set(sail_status) | set(spike_status) | set(cases), key=str.casefold)

    artifact_index: dict[str, list[Path]] = defaultdict(list)
    if artifact_root.is_dir():
        for path in artifact_root.rglob("*"):
            if path.is_file():
                artifact_index[path.name].append(path)

    def extension_for_test(name: str) -> str:
        for suffix in (".sig.log", ".results", ".sig.elf"):
            matches = artifact_index.get(name + suffix, [])
            if not matches:
                continue
            try:
                relative = matches[0].relative_to(artifact_root)
            except ValueError:
                continue
            if len(relative.parts) >= 2:
                return relative.parts[-2]
        return "Unclassified"

    rows: list[str] = []
    extension_rows: dict[str, list[str]] = defaultdict(list)
    extension_tests: dict[str, list[str]] = defaultdict(list)
    vf2_counts = Counter()
    sail_counts = Counter(sail_status.values())
    spike_counts = Counter(spike_status.values())

    for name in names:
        case = cases.get(name, {})
        extension = extension_for_test(name)
        extension_slug = safe_name(extension)
        extension_tests[extension].append(name)
        vf2 = str(case.get("status", "NOT RUN"))
        if name in cases:
            vf2_counts[vf2] += 1
        category = str(case.get("category", ""))
        detail_dir = site_root / "tests" / safe_name(name)
        detail_dir.mkdir(parents=True, exist_ok=True)
        case_dir = run_root / "per_case" / name

        raw_links: list[str] = []
        if case_dir.is_dir():
            for filename in ("report.md", "uart.log", "test.trap_report", "evidence.json"):
                path = case_dir / filename
                if path.is_file():
                    rendered = publish_file(
                        path,
                        Path("downloads") / "per_case" / safe_name(name) / filename,
                        filename,
                    )
                    if rendered:
                        raw_links.append(rendered)

        for suffix, label in ((".sig.log", "Sail log"), (".results", "Sail results"), (".sig", "Sail signature")):
            matches = artifact_index.get(name + suffix, [])
            if matches:
                rendered = publish_file(
                    matches[0],
                    Path("downloads") / "sail" / safe_name(name) / matches[0].name,
                    label,
                )
                if rendered:
                    raw_links.append(rendered)

        report_path = case_dir / "report.md"
        report_text = ""
        if report_path.is_file():
            report_text = report_path.read_text(encoding="utf-8", errors="replace")
        elif case:
            report_text = json.dumps(case, indent=2, sort_keys=True)
        else:
            report_text = "No VF2 per-case report was generated."

        detail_html = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(name)} results</title><link rel="stylesheet" href="../../style.css"></head>
<body><main><p><a href="../../index.html">&larr; Result Summary</a> &middot;
<a href="../../extensions/{quote(extension_slug)}/index.html">{html.escape(extension)} extension</a> &middot;
<a href="../../history/tests/{quote(safe_name(name))}/index.html">Test history</a></p>
<h1>{html.escape(name)}</h1>
<p class="subtitle">Extension: <strong>{html.escape(extension)}</strong> &middot;
Sail version: <strong>{html.escape(sail_version)}</strong></p>
<div class="status-grid">
<div><strong>Sail</strong>{badge(sail_status.get(name, "NOT RUN"))}</div>
<div><strong>Spike</strong>{badge(spike_status.get(name, "NOT RUN"))}</div>
<div><strong>VF2</strong>{badge(vf2)}</div></div>
<h2>Reported ACT/Sail tickets</h2>
<p>{issue_links_for_test(name) or 'No GitHub ticket is recorded for this test in the tracking sheet.'}</p>
<h2>Downloads and raw logs</h2><p>{' &middot; '.join(raw_links) if raw_links else 'No archived files are available.'}</p>
<h2>Test report</h2><pre>{html.escape(report_text)}</pre>
</main></body></html>"""
        (detail_dir / "index.html").write_text(detail_html, encoding="utf-8")

        detail_url = f"tests/{quote(safe_name(name))}/index.html"
        rows.append(
            "<tr>"
            f'<td><a href="{detail_url}">{html.escape(name)}</a></td>'
            f'<td><a href="extensions/{quote(extension_slug)}/index.html">{html.escape(extension)}</a></td>'
            f"<td>{html.escape(sail_version)}</td>"
            f"<td>{badge(sail_status.get(name, 'NOT RUN'))}</td>"
            f"<td>{badge(spike_status.get(name, 'NOT RUN'))}</td>"
            f"<td>{badge(vf2)}</td>"
            f"<td>{html.escape(category or '—')}</td>"
            f"<td>{issue_links_for_test(name) or '—'}</td>"
            f"<td>{' &middot; '.join(raw_links[:5]) if raw_links else '—'}</td>"
            "</tr>"
        )
        extension_rows[extension].append(
            "<tr>"
            f'<td><a href="../../tests/{quote(safe_name(name))}/index.html">{html.escape(name)}</a></td>'
            f"<td>{html.escape(sail_version)}</td>"
            f"<td>{badge(sail_status.get(name, 'NOT RUN'))}</td>"
            f"<td>{badge(spike_status.get(name, 'NOT RUN'))}</td>"
            f"<td>{badge(vf2)}</td>"
            f"<td>{html.escape(category or '—')}</td>"
            f"<td>{issue_links_for_test(name) or '—'}</td>"
            f"<td>{' &middot; '.join(raw_links[:5]) if raw_links else '—'}</td>"
            "</tr>"
        )

    summary_path = run_root / "summary.md"
    if summary_path.is_file():
        shutil.copy2(summary_path, site_root / "summary.md")
    tracking_comparison = state_root / "tracking_sheet_comparison.csv"
    if tracking_comparison.is_file():
        shutil.copy2(tracking_comparison, site_root / tracking_comparison.name)

    def count_card(title: str, counts: Counter) -> str:
        total = sum(counts.values())
        passed = counts.get("PASS", 0)
        breakdown = " ".join(
            f"{html.escape(str(key))}: <strong>{value}</strong>" for key, value in sorted(counts.items())
        ) or "No results"
        ratio = f'<div class="ratio"><strong>{passed} / {total}</strong> PASS</div>' if total else '<div class="ratio">Not run</div>'
        return f'<div class="card"><h2>{html.escape(title)}</h2>{ratio}<p>{breakdown}</p></div>'

    def pass_ratio(statuses: dict[str, str], group: list[str]) -> tuple[int, int]:
        values = [statuses[name] for name in group if name in statuses]
        return sum(value == "PASS" for value in values), len(values)

    extension_summary_rows: list[str] = []
    for extension in sorted(extension_tests, key=str.casefold):
        group = extension_tests[extension]
        sail_pass, sail_total = pass_ratio(sail_status, group)
        spike_pass, spike_total = pass_ratio(spike_status, group)
        vf2_map = {name: str(cases[name].get("status", "")) for name in group if name in cases}
        vf2_pass, vf2_total = pass_ratio(vf2_map, group)
        slug = safe_name(extension)
        extension_summary_rows.append(
            "<tr>"
            f'<td><a href="extensions/{quote(slug)}/index.html">{html.escape(extension)}</a></td>'
            f"<td>{len(group)}</td>"
            f"<td>{html.escape(sail_version)}</td>"
            f"<td><strong>{sail_pass} / {sail_total}</strong></td>"
            f"<td><strong>{spike_pass} / {spike_total}</strong></td>"
            f"<td><strong>{vf2_pass} / {vf2_total}</strong></td>"
            "</tr>"
        )

        extension_html = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(extension)} extension results</title><link rel="stylesheet" href="../../style.css"></head>
<body><main><p><a href="../../index.html">&larr; Overall Result Summary</a> &middot;
<a href="../../history/suites/{quote(slug)}/index.html">Extension history</a></p>
<h1>{html.escape(extension)} Extension Results</h1>
<p class="subtitle">{len(group)} discovered tests &middot;
Sail version: <strong>{html.escape(sail_version)}</strong></p>
<div class="cards">
<div class="card"><h2>Sail</h2><div class="ratio"><strong>{sail_pass} / {sail_total}</strong> PASS</div></div>
<div class="card"><h2>Spike</h2><div class="ratio"><strong>{spike_pass} / {spike_total}</strong> PASS</div></div>
<div class="card"><h2>VF2</h2><div class="ratio"><strong>{vf2_pass} / {vf2_total}</strong> PASS</div></div></div>
<h2>All tests in this extension</h2>
<table><thead><tr><th>Test</th><th>Sail version</th><th>Sail</th><th>Spike</th><th>VF2</th><th>Category</th><th>ACT/Sail tickets</th><th>Logs and downloads</th></tr></thead>
<tbody>{''.join(extension_rows[extension])}</tbody></table>
</main></body></html>"""
        extension_dir = site_root / "extensions" / slug
        extension_dir.mkdir(parents=True, exist_ok=True)
        (extension_dir / "index.html").write_text(extension_html, encoding="utf-8")

    issue_rows: list[str] = []
    ticket_tests: dict[str, list[str]] = defaultdict(list)
    for test_name, keys in issue_catalog.get("tests", {}).items():
        for key in keys:
            ticket_tests[key].append(test_name)
    for key, ticket in sorted(
        issue_catalog.get("tickets", {}).items(),
        key=lambda item: (str(item[1].get("tracker", "")), int(item[1].get("number", 0))),
    ):
        url = str(ticket.get("url", ""))
        tracker = str(ticket.get("tracker", "GitHub"))
        number = str(ticket.get("number", ""))
        kind = str(ticket.get("kind", "issue"))
        state = str(ticket.get("state", "unknown")).upper()
        title = str(ticket.get("title", ""))
        label = f"{tracker}{' PR' if kind == 'pull request' else ''} #{number}"
        rendered_tests: list[str] = []
        for test_name in sorted(ticket_tests.get(key, []), key=str.casefold):
            if (site_root / "tests" / safe_name(test_name) / "index.html").is_file():
                rendered_tests.append(
                    f'<a href="../tests/{quote(safe_name(test_name))}/index.html">{html.escape(test_name)}</a>'
                )
            else:
                rendered_tests.append(html.escape(test_name))
        test_links = " ".join(rendered_tests)
        issue_rows.append(
            "<tr>"
            f'<td><a class="ticket {html.escape(state.lower())}" '
            f'href="{html.escape(url, quote=True)}" target="_blank" '
            f'rel="noopener noreferrer">{html.escape(label)}</a></td>'
            f"<td>{badge(state)}</td>"
            f"<td>{html.escape(title)}</td>"
            f"<td>{test_links or '—'}</td>"
            "</tr>"
        )
    issues_dir = site_root / "issues"
    issues_dir.mkdir(exist_ok=True)
    issues_html = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Tracked ACT and Sail GitHub tickets</title><link rel="stylesheet" href="../style.css"></head>
<body><main><p><a href="../index.html">&larr; Result Summary</a></p>
<h1>Tracked ACT and Sail GitHub Tickets</h1>
<p class="subtitle">{len(issue_catalog.get('tickets', {}))} verified GitHub tickets mapped from the tracking sheet's
Failure cause column to {len(issue_catalog.get('tests', {}))} test cases.</p>
<table><thead><tr><th>Ticket</th><th>State</th><th>Official GitHub title</th><th>Related tests</th></tr></thead>
<tbody>{''.join(issue_rows) if issue_rows else '<tr><td colspan="4">No tracked tickets were found.</td></tr>'}</tbody></table>
</main></body></html>"""
    (issues_dir / "index.html").write_text(issues_html, encoding="utf-8")

    comparison_nav = ""
    if tracking_comparison.is_file():
        with tracking_comparison.open(newline="", encoding="utf-8", errors="replace") as stream:
            comparison_rows = list(csv.DictReader(stream))
        spike_comparisons = Counter(row.get("spike_comparison", "") for row in comparison_rows)
        vf2_comparisons = Counter(row.get("vf2_comparison", "") for row in comparison_rows)
        tracking_rows: list[str] = []
        for row in comparison_rows:
            test_name = str(row.get("test_name", ""))
            if (site_root / "tests" / safe_name(test_name) / "index.html").is_file():
                test_cell = (
                    f'<a href="../tests/{quote(safe_name(test_name))}/index.html">'
                    f"{html.escape(test_name)}</a>"
                )
            else:
                test_cell = html.escape(test_name)
            failure_cause = str(row.get("failure_cause", ""))
            failure_cell = (
                f"<details><summary>Show failure cause</summary><pre>{html.escape(failure_cause)}</pre></details>"
                if failure_cause
                else "—"
            )
            tracking_rows.append(
                "<tr>"
                f"<td>{test_cell}</td>"
                f"<td>{html.escape(str(row.get('sheet_spike', '') or '—'))}</td>"
                f"<td>{badge(str(row.get('jenkins_spike', 'NOT RUN')))}</td>"
                f"<td>{badge(str(row.get('spike_comparison', 'UNKNOWN')))}</td>"
                f"<td>{html.escape(str(row.get('sheet_vf2', '') or '—'))}</td>"
                f"<td>{badge(str(row.get('jenkins_vf2', 'NOT RUN')))}</td>"
                f"<td>{badge(str(row.get('vf2_comparison', 'UNKNOWN')))}</td>"
                f"<td>{html.escape(str(row.get('sheet_workflow_status', '') or '—'))}</td>"
                f"<td>{html.escape(str(row.get('assignee', '') or '—'))}</td>"
                f"<td>{issue_links_for_test(test_name) or '—'}</td>"
                f"<td>{failure_cell}</td>"
                "</tr>"
            )
        tracking_dir = site_root / "tracking"
        tracking_dir.mkdir(exist_ok=True)
        tracking_html = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Tracking sheet versus Jenkins</title><link rel="stylesheet" href="../style.css"></head>
<body><main><p><a href="../index.html">&larr; Result Summary</a> &middot;
<a href="../downloads/tracking_sheet_comparison.csv" download>Download comparison.csv</a></p>
<h1>Tracking Sheet versus Jenkins</h1>
<p class="subtitle">{len(comparison_rows)} tests from the public tracking sheet compared with
<strong>{html.escape(state_root.name)}</strong>.</p>
<div class="metrics">
<div class="metric">Spike matches<strong>{spike_comparisons.get('MATCH', 0)}</strong></div>
<div class="metric">Spike differences<strong>{spike_comparisons.get('DIFFERENT', 0)}</strong></div>
<div class="metric">VF2 matches<strong>{vf2_comparisons.get('MATCH', 0)}</strong></div>
<div class="metric">VF2 differences<strong>{vf2_comparisons.get('DIFFERENT', 0)}</strong></div></div>
<p class="muted">A difference means the latest Jenkins verdict differs from the result text currently
recorded in the sheet; it does not by itself identify which result is newer or correct.</p>
<table><thead><tr><th>Test</th><th>Sheet Spike</th><th>Jenkins Spike</th><th>Spike comparison</th>
<th>Sheet VF2</th><th>Jenkins VF2</th><th>VF2 comparison</th><th>Sheet status</th>
<th>Assignee</th><th>ACT/Sail tickets</th><th>Failure cause</th></tr></thead>
<tbody>{''.join(tracking_rows)}</tbody></table>
</main></body></html>"""
        (tracking_dir / "index.html").write_text(tracking_html, encoding="utf-8")
        comparison_nav = '<a href="tracking/index.html">Sheet comparison &rarr;</a>'

    complete_uart = publish_file(
        run_root / "uart_capture.log",
        Path("downloads") / "uart_capture.log",
        "Complete UART capture",
    )
    cases_csv_link = publish_file(
        run_root / "cases.csv",
        Path("downloads") / "cases.csv",
        "Download cases.csv",
        force_download=True,
    )
    summary_link = publish_file(
        summary_path,
        Path("downloads") / "summary.md",
        "Raw summary.md",
    )
    complete_zip = publish_file(
        state_root.parent / f"{state_root.name}-complete.zip",
        Path("downloads") / f"{state_root.name}-complete.zip",
        "Download complete Jenkins run ZIP",
        force_download=True,
        allow_missing_source=True,
    )
    comparison_link = publish_file(
        tracking_comparison,
        Path("downloads") / "tracking_sheet_comparison.csv",
        "Download tracking-sheet comparison.csv",
        force_download=True,
    )
    top_links = " &middot; ".join(
        item for item in (complete_zip, comparison_link, summary_link, cases_csv_link, complete_uart) if item
    )

    index_html = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>VF2 privileged-test result summary</title><link rel="stylesheet" href="style.css"></head>
<body><main><h1>VF2 Privileged-Test Result Summary</h1>
<p class="subtitle">Run <strong>{html.escape(state_root.name)}</strong> &middot;
{len(names)} discovered test cases &middot; Sail version: <strong>{html.escape(sail_version)}</strong></p>
<p class="primary-nav"><a href="history/index.html">Open execution history dashboard &rarr;</a>
<a href="issues/index.html">Tracked ACT/Sail tickets &rarr;</a>
{comparison_nav}</p>
<div class="cards">{count_card('Sail reference', sail_counts)}{count_card('Spike reference', spike_counts)}{count_card('VF2 hardware', vf2_counts)}</div>
<p>{top_links or 'Top-level run artifacts are not available.'}</p>
<h2>Results by extension</h2>
<p>PASS / executed total is reported independently for each execution target. Select an extension for its complete test list.</p>
<table><thead><tr><th>Extension</th><th>Discovered tests</th><th>Sail version</th><th>Sail PASS / Total</th><th>Spike PASS / Total</th><th>VF2 PASS / Total</th></tr></thead>
<tbody>{''.join(extension_summary_rows) if extension_summary_rows else '<tr><td colspan="5">No extension results were generated.</td></tr>'}</tbody></table>
<h2>Logs for each test case</h2>
<p>Select a test to view its report. Raw files are served by Jenkins Artifacts and can be viewed or downloaded.</p>
<table><thead><tr><th>Test</th><th>Extension</th><th>Sail version</th><th>Sail</th><th>Spike</th><th>VF2</th><th>Category</th><th>ACT/Sail tickets</th><th>Quick links</th></tr></thead>
<tbody>{''.join(rows) if rows else '<tr><td colspan="8">No test results were generated.</td></tr>'}</tbody></table>
</main></body></html>"""
    (site_root / "index.html").write_text(index_html, encoding="utf-8")

    css = """
:root { color-scheme: light dark; font-family: system-ui, sans-serif; }
body { margin: 0; background: #f5f7fa; color: #1f2937; }
main { max-width: 1500px; margin: 0 auto; padding: 28px; }
h1 { margin-bottom: 4px; } .subtitle { color: #5f6b7a; }
.ratio { font-size: 1.15rem; margin-top: 8px; }
.cards, .status-grid { display: flex; flex-wrap: wrap; gap: 14px; margin: 20px 0; }
.card, .status-grid > div { background: white; border: 1px solid #d8dee8; border-radius: 8px; padding: 12px 16px; }
.status-grid > div { display: flex; align-items: center; gap: 12px; }
table { width: 100%; border-collapse: collapse; background: white; }
th, td { border: 1px solid #d8dee8; padding: 8px 10px; text-align: left; vertical-align: top; }
th { background: #e9eef5; position: sticky; top: 0; }
tr:nth-child(even) { background: #f8fafc; }
.badge { display: inline-block; border-radius: 999px; padding: 2px 9px; font-size: .8rem; font-weight: 700; white-space: nowrap; }
.badge.pass { background: #d1fae5; color: #065f46; } .badge.fail { background: #fee2e2; color: #991b1b; }
.badge.skip { background: #fef3c7; color: #92400e; } .badge.unknown { background: #e5e7eb; color: #374151; }
.badge.open { background: #d1fae5; color: #065f46; } .badge.closed { background: #e5e7eb; color: #374151; }
.primary-nav a { display: inline-block; padding: 9px 14px; border-radius: 7px; background: #075985; color: white; font-weight: 700; text-decoration: none; }
.metrics { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 14px; margin: 20px 0; }
.metric { background: white; border: 1px solid #d8dee8; border-radius: 8px; padding: 14px 16px; }
.metric strong { display: block; font-size: 1.45rem; margin-top: 4px; }
.muted { color: #64748b; } .nowrap { white-space: nowrap; }
.trend { display: flex; flex-wrap: wrap; gap: 4px; min-width: 130px; }
.trend .badge { padding: 2px 7px; }
.ticket { display: inline-block; margin: 2px 4px 2px 0; border: 1px solid #94a3b8; border-radius: 6px; padding: 3px 7px; text-decoration: none; white-space: nowrap; }
.ticket small { margin-left: 4px; font-weight: 700; } .ticket.open small { color: #047857; } .ticket.closed small { color: #64748b; }
pre { overflow: auto; white-space: pre-wrap; background: #111827; color: #e5e7eb; padding: 18px; border-radius: 8px; }
a { color: #075985; } @media (prefers-color-scheme: dark) { body { background: #111827; color: #e5e7eb; } .card, .metric, table, .status-grid > div { background: #1f2937; } th { background: #374151; } tr:nth-child(even) { background: #252f3f; } a { color: #7dd3fc; } }
"""
    (site_root / "style.css").write_text(css.strip() + "\n", encoding="utf-8")

    history_summary = build_history_dashboard(
        workspace=workspace,
        state_root=state_root,
        site_root=site_root,
        build_url=build_url,
        fallback_extension=extension_for_test,
        issue_links=issue_links_for_test,
    )

    vf2_pass = vf2_counts.get("PASS", 0)
    vf2_fail = vf2_counts.get("FAIL", 0)
    description = f"VF2: {vf2_pass} PASS / {vf2_fail} FAIL | Sail runnable: {sail_counts.get('PASS', 0)}"
    (state_root / "build_description.txt").write_text(description + "\n", encoding="utf-8")
    print(f"Result dashboard: {site_root / 'index.html'}")
    print(
        "History dashboard: "
        f"{site_root / 'history' / 'index.html'} "
        f"({history_summary['runs']} runs, {history_summary['suites']} suites, "
        f"{history_summary['tests']} tests)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
