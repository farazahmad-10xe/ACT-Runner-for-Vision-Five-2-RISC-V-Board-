#!/usr/bin/env python3
"""Stage the tracked and freshly generated RV64 + privileged ACT tests."""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


TEST_ROOTS = ("env", "priv", "rv64i")


def link_file(source_file: Path, source_root: Path, destination: Path) -> None:
    target = destination / source_file.relative_to(source_root)
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() or target.is_symlink():
        target.unlink()
    target.symlink_to(source_file.resolve())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--repository-root", type=Path, required=True)
    parser.add_argument("--generated-source", type=Path)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.resolve()
    repository_root = args.repository_root.resolve()
    destination = args.destination.resolve()
    if not all((source / root).is_dir() for root in TEST_ROOTS):
        raise SystemExit(f"ACT source tree is incomplete: {source}")

    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)

    tracked = subprocess.run(
        [
            "git",
            "-C",
            str(repository_root),
            "ls-files",
            "-z",
            "--",
            *(f"tests/{root}" for root in TEST_ROOTS),
        ],
        check=True,
        capture_output=True,
    ).stdout.split(b"\0")

    tracked_files = tracked_tests = 0
    for raw_path in tracked:
        if not raw_path:
            continue
        path = repository_root / raw_path.decode()
        if not path.is_file():
            continue
        link_file(path, source, destination)
        tracked_files += 1
        tracked_tests += int(path.suffix == ".S")

    generated_files = generated_tests = 0
    if args.generated_source:
        generated_source = args.generated_source.resolve()
        for root_name in ("priv", "rv64i"):
            root = generated_source / root_name
            if not root.is_dir():
                continue
            for path in sorted(item for item in root.rglob("*") if item.is_file()):
                link_file(path, generated_source, destination)
                generated_files += 1
                generated_tests += int(path.suffix == ".S")

    total_tests = sum(1 for path in destination.rglob("*.S") if path.is_file())
    print(
        "Staged official RV64 + privileged ACT tree: "
        f"tracked_tests={tracked_tests} generated_tests={generated_tests} "
        f"selected_files={tracked_files + generated_files} tests={total_tests} "
        f"destination={destination}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
