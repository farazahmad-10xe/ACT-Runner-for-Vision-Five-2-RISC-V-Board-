#!/usr/bin/env python3
"""Install repository Jenkins job definitions and request a safe config reload."""

from __future__ import annotations

import argparse
import base64
import http.cookiejar
import html
import json
import os
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path


JOBS = (
    (
        "vf2-validation-dashboard",
        "Jenkinsfile.dashboard",
        "ci/jenkins/job-config-dashboard.xml",
    ),
    (
        "vf2-privileged-sanity",
        "Jenkinsfile.sanity",
        "ci/jenkins/job-config-sanity.xml",
    ),
    (
        "vf2-privileged-weekly",
        "Jenkinsfile",
        "ci/jenkins/job-config.xml",
    ),
    (
        "bpif3-privileged-weekly",
        "Jenkinsfile.bpif3-weekly",
        "ci/jenkins/job-config-bpif3.xml",
    ),
    (
        "vf2-act-update-validation",
        "Jenkinsfile.act-update-validation",
        "ci/jenkins/job-config-act-update.xml",
    ),
)


def render_job(repo_root: Path, jenkinsfile: str, template: str) -> str:
    pipeline = (repo_root / jenkinsfile).read_text(encoding="utf-8")
    source = (repo_root / template).read_text(encoding="utf-8")
    rendered = source.replace("__JENKINSFILE__", html.escape(pipeline))
    ET.fromstring(rendered)
    return rendered


def atomic_write(path: Path, data: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".xml.tmp")
    temporary.write_text(data, encoding="utf-8")
    os.chmod(temporary, 0o644)
    temporary.replace(path)


def request(
    opener: urllib.request.OpenerDirector,
    url: str,
    authorization: str,
    *,
    method: str = "GET",
    headers: dict[str, str] | None = None,
    timeout: int = 15,
) -> bytes:
    request_headers = {"Authorization": authorization}
    request_headers.update(headers or {})
    req = urllib.request.Request(
        url,
        data=b"" if method == "POST" else None,
        headers=request_headers,
        method=method,
    )
    with opener.open(req, timeout=timeout) as response:
        return response.read()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--jenkins-home", type=Path, required=True)
    parser.add_argument("--jenkins-url", default="http://127.0.0.1:8080")
    parser.add_argument("--admin-user", default="vf2admin")
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    jenkins_home = args.jenkins_home.resolve()
    password_file = jenkins_home / "secrets" / "vf2AdminPassword"
    password = password_file.read_text(encoding="utf-8").strip()
    token = base64.b64encode(
        f"{args.admin_user}:{password}".encode("utf-8")
    ).decode("ascii")
    authorization = f"Basic {token}"
    cookie_jar = http.cookiejar.CookieJar()
    opener = urllib.request.build_opener(
        urllib.request.HTTPCookieProcessor(cookie_jar)
    )

    installed: list[str] = []
    for job_name, jenkinsfile, template in JOBS:
        rendered = render_job(repo_root, jenkinsfile, template)
        config = jenkins_home / "jobs" / job_name / "config.xml"
        atomic_write(config, rendered)
        installed.append(job_name)

    base_url = args.jenkins_url.rstrip("/")
    crumb_data = json.loads(
        request(opener, f"{base_url}/crumbIssuer/api/json", authorization)
    )
    crumb_headers = {
        str(crumb_data["crumbRequestField"]): str(crumb_data["crumb"])
    }
    try:
        request(
            opener,
            f"{base_url}/reload",
            authorization,
            method="POST",
            headers=crumb_headers,
        )
    except urllib.error.HTTPError as error:
        # Jenkins may close/reject requests briefly while reloading. Only
        # explicit authorization/request errors should stop installation.
        if error.code not in (502, 503):
            raise

    missing = set(installed)
    for _ in range(60):
        for job_name in tuple(missing):
            try:
                request(
                    opener,
                    f"{base_url}/job/{job_name}/api/json",
                    authorization,
                    timeout=5,
                )
            except (urllib.error.URLError, TimeoutError):
                continue
            else:
                missing.remove(job_name)
        if not missing:
            break
        time.sleep(2)
    if missing:
        raise SystemExit(
            "Jenkins reloaded, but these jobs were not visible: "
            + ", ".join(sorted(missing))
        )

    print("Installed and reloaded Jenkins jobs:")
    for job_name in installed:
        print(f"  {base_url}/job/{job_name}/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
