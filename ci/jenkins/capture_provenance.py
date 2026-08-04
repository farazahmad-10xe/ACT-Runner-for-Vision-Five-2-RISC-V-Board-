#!/usr/bin/env python3
"""Capture reproducible source and artifact provenance for a Jenkins run."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import platform
import shutil
import subprocess
from pathlib import Path


def command(
    args: list[str], cwd: Path, *, check: bool = True, text: bool = True
) -> str | bytes:
    result = subprocess.run(
        args,
        cwd=cwd,
        check=check,
        capture_output=True,
        text=text,
    )
    return result.stdout


def git_text(repo: Path, *args: str, check: bool = True) -> str:
    return str(command(["git", *args], repo, check=check)).strip()


def remote_map(repo: Path) -> dict[str, dict[str, str]]:
    remotes: dict[str, dict[str, str]] = {}
    output = git_text(repo, "remote", "-v", check=False)
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 3:
            continue
        name, url, kind = fields[0], fields[1], fields[2].strip("()")
        remotes.setdefault(name, {})[kind] = url
    return remotes


def repo_record(repo: Path) -> dict[str, object]:
    status = git_text(repo, "status", "--short", check=False)
    tracked_status = git_text(
        repo, "status", "--short", "--untracked-files=no", check=False
    )
    return {
        "path": str(repo),
        "head": git_text(repo, "rev-parse", "HEAD"),
        "branch": git_text(repo, "branch", "--show-current", check=False)
        or "(detached)",
        "describe": git_text(
            repo, "describe", "--always", "--dirty", "--broken", check=False
        ),
        "remotes": remote_map(repo),
        "dirty_entries": len(status.splitlines()) if status else 0,
        "tracked_dirty_entries": (
            len(tracked_status.splitlines()) if tracked_status else 0
        ),
    }


def version_output(args: list[str], cwd: Path) -> str:
    try:
        result = subprocess.run(
            args,
            cwd=cwd,
            check=False,
            capture_output=True,
            text=True,
            timeout=30,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired) as error:
        return f"unavailable: {error}"
    output = (result.stdout + result.stderr).strip()
    return output.splitlines()[0] if output else f"rc={result.returncode}"


def write_git_evidence(repo: Path, prefix: str, destination: Path) -> None:
    (destination / f"{prefix}_status.txt").write_text(
        git_text(repo, "status", "--short", check=False) + "\n",
        encoding="utf-8",
    )
    (destination / f"{prefix}_tracked_worktree.patch").write_bytes(
        bytes(
            command(
                ["git", "diff", "--binary", "--no-ext-diff"],
                repo,
                text=False,
            )
        )
    )
    (destination / f"{prefix}_tracked_index.patch").write_bytes(
        bytes(
            command(
                ["git", "diff", "--cached", "--binary", "--no-ext-diff"],
                repo,
                text=False,
            )
        )
    )
    (destination / f"{prefix}_recent_commits.txt").write_text(
        git_text(
            repo,
            "log",
            "--date=iso-strict",
            "--pretty=format:%H%x09%P%x09%ad%x09%an%x09%s",
            "-n",
            "50",
            check=False,
        )
        + "\n",
        encoding="utf-8",
    )


def copy_control_snapshot(
    repo_root: Path, act_root: Path, destination: Path, act_inputs: list[str]
) -> None:
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)

    control_paths = [
        repo_root / "Jenkinsfile",
        repo_root / "Jenkinsfile.bpif3-weekly",
        repo_root / "Jenkinsfile.act-update-validation",
        repo_root / "ci" / "jenkins",
    ]
    for source in control_paths:
        if not source.exists():
            continue
        target = destination / source.relative_to(repo_root)
        if source.is_dir():
            shutil.copytree(
                source,
                target,
                ignore=shutil.ignore_patterns("__pycache__", "*.pyc"),
            )
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)

    for relative in [*act_inputs, "framework/src/act/config.py"]:
        source = act_root / relative
        if source.is_file():
            target = destination / "external/riscv-arch-test" / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def hash_tree(root: Path, output: Path) -> int:
    rows: list[str] = []
    if root.exists():
        paths: list[Path] = []
        for directory, _, filenames in os.walk(root, followlinks=True):
            paths.extend(Path(directory) / filename for filename in filenames)
        for path in sorted(paths, key=lambda candidate: str(candidate)):
            rows.append(f"{hash_file(path)}  {path.relative_to(root)}")
    output.write_text("".join(f"{row}\n" for row in rows), encoding="utf-8")
    return len(rows)


def preflight(args: argparse.Namespace, provenance: Path) -> None:
    repo_root = args.repo_root.resolve()
    act_root = args.act_root.resolve()
    data = {
        "schema": 1,
        "captured_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "expected_act_revision": args.expected_act_revision,
        "host": {
            "platform": platform.platform(),
            "uname": " ".join(platform.uname()),
        },
        "tools": {
            "python": version_output(["python3", "--version"], repo_root),
            "uv": version_output(["uv", "--version"], repo_root),
            "sail": version_output(["sail_riscv_sim", "--version"], repo_root),
            "spike": version_output(["spike", "--version"], repo_root),
            "compiler": version_output(
                ["riscv64-unknown-elf-gcc", "--version"], repo_root
            ),
            "make": version_output(["make", "--version"], repo_root),
            "mkimage": version_output(["mkimage", "-V"], repo_root),
        },
        "runner": repo_record(repo_root),
        "act": repo_record(act_root),
    }
    (provenance / "source_manifest.json").write_text(
        json.dumps(data, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    write_git_evidence(repo_root, "runner", provenance)
    write_git_evidence(act_root, "act", provenance)

    official_refs = ("official/act4", "upstream/act4")
    for official_ref in official_refs:
        if (
            subprocess.run(
                ["git", "rev-parse", "--verify", f"{official_ref}^{{commit}}"],
                cwd=act_root,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            ).returncode
            != 0
        ):
            continue
        base = git_text(act_root, "merge-base", "HEAD", official_ref)
        (provenance / "act_official_comparison.txt").write_text(
            f"official_ref={official_ref}\n"
            f"official_head={git_text(act_root, 'rev-parse', official_ref)}\n"
            f"merge_base={base}\n"
            f"left_right_count={git_text(act_root, 'rev-list', '--left-right', '--count', f'HEAD...{official_ref}')}\n",
            encoding="utf-8",
        )
        (provenance / "act_vf2_overlay.patch").write_bytes(
            bytes(
                command(
                    [
                        "git",
                        "diff",
                        "--binary",
                        "--no-ext-diff",
                        base,
                        "HEAD",
                    ],
                    act_root,
                    text=False,
                )
            )
        )
        break

    snapshot = provenance / "control_snapshot"
    copy_control_snapshot(repo_root, act_root, snapshot, args.act_input)
    hash_tree(snapshot, provenance / "control_snapshot.sha256")


def prepared(args: argparse.Namespace, provenance: Path) -> None:
    record: dict[str, object] = {
        "schema": 1,
        "captured_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "expected_act_revision": args.expected_act_revision,
    }
    if args.test_root:
        test_root = args.test_root.resolve()
        record["test_root"] = str(test_root)
        record["test_input_files"] = hash_tree(
            test_root, provenance / "test_inputs.sha256"
        )
    if args.pack_list and args.pack_list.is_file():
        rows: list[str] = []
        elf_count = 0
        for raw in args.pack_list.read_text(encoding="utf-8").splitlines():
            if not raw.strip() or raw.lstrip().startswith("#"):
                continue
            elf = Path(raw.strip()).resolve()
            rows.append(f"{hash_file(elf)}  {elf}")
            elf_count += 1
        (provenance / "packed_elfs.sha256").write_text(
            "".join(f"{row}\n" for row in rows), encoding="utf-8"
        )
        record["pack_list"] = str(args.pack_list.resolve())
        record["packed_elfs"] = elf_count
    if args.hardware_artifacts:
        hardware_artifacts = args.hardware_artifacts.resolve()
        record["hardware_artifacts"] = str(hardware_artifacts)
        record["hardware_artifact_files"] = hash_tree(
            hardware_artifacts,
            provenance / "hardware_artifacts.sha256",
        )
    (provenance / "prepared_manifest.json").write_text(
        json.dumps(record, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--act-root", type=Path, required=True)
    parser.add_argument("--state-root", type=Path, required=True)
    parser.add_argument("--phase", choices=("preflight", "prepared"), required=True)
    parser.add_argument("--expected-act-revision", required=True)
    parser.add_argument("--test-root", type=Path)
    parser.add_argument("--pack-list", type=Path)
    parser.add_argument("--hardware-artifacts", type=Path)
    parser.add_argument("--act-input", action="append", default=[])
    args = parser.parse_args()

    provenance = args.state_root.resolve() / "provenance"
    provenance.mkdir(parents=True, exist_ok=True)
    if args.phase == "preflight":
        preflight(args, provenance)
    else:
        prepared(args, provenance)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
