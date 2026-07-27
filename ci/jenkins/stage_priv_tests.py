#!/usr/bin/env python3
"""Create a clean ACT tree containing only official privileged tests.

Static tests are selected from files tracked by the ACT Git checkout. Generated
tests are taken from a separate, freshly-created testgen output tree. This
prevents local probes and other untracked files under ``tests/priv`` from
leaking into Jenkins regressions.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


def link_file(source_file: Path, source_root: Path, destination: Path) -> None:
    rel = source_file.relative_to(source_root)
    destination_file = destination / rel
    destination_file.parent.mkdir(parents=True, exist_ok=True)
    if destination_file.exists() or destination_file.is_symlink():
        destination_file.unlink()
    destination_file.symlink_to(source_file.resolve())


def csv_names(raw: str) -> set[str]:
    return {name.strip() for name in raw.split(",") if name.strip()}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument(
        "--repository-root",
        type=Path,
        required=True,
        help="ACT Git checkout containing the tracked static test tree",
    )
    parser.add_argument(
        "--generated-source",
        type=Path,
        help="Fresh testgen output root containing priv/<suite> directories",
    )
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument(
        "--include-top-level",
        required=True,
        help="Comma-separated tracked suite directories below tests/priv",
    )
    parser.add_argument(
        "--include-generated-top-level",
        default="",
        help="Comma-separated suite directories below GENERATED_SOURCE/priv",
    )
    args = parser.parse_args()

    source = args.source.resolve()
    repository_root = args.repository_root.resolve()
    destination = args.destination.resolve()
    source_priv = source / "priv"
    source_env = source / "env"
    if (
        not source_priv.is_dir()
        or not source_env.is_dir()
        or not (repository_root / ".git").exists()
    ):
        raise SystemExit(f"ACT source tree is incomplete: {source}")

    tracked_suites = csv_names(args.include_top_level)
    generated_suites = csv_names(args.include_generated_top_level)
    if not tracked_suites and not generated_suites:
        raise SystemExit("No tracked or generated suite names were provided")

    missing_tracked = sorted(
        name for name in tracked_suites if not (source_priv / name).is_dir()
    )
    available_tracked = tracked_suites - set(missing_tracked)
    if missing_tracked:
        print(
            "Tracked suites with no source directory (skipped):\n  "
            + "\n  ".join(missing_tracked)
        )

    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    (destination / "env").symlink_to(source_env, target_is_directory=True)

    file_count = 0
    test_count = 0
    tracked_paths: list[bytes] = []
    if available_tracked:
        tracked_paths = subprocess.run(
            [
                "git",
                "-C",
                str(repository_root),
                "ls-files",
                "-z",
                "--",
                *(f"tests/priv/{name}" for name in sorted(available_tracked)),
            ],
            check=True,
            capture_output=True,
        ).stdout.split(b"\0")
    for raw_path in tracked_paths:
        if not raw_path:
            continue
        repository_file = repository_root / raw_path.decode()
        if not repository_file.is_file():
            continue
        link_file(repository_file, repository_root / "tests", destination)
        file_count += 1
        if repository_file.suffix == ".S":
            test_count += 1

    generated_test_count = 0
    if args.generated_source:
        generated_source = args.generated_source.resolve()
        generated_priv = generated_source / "priv"
        missing_generated = sorted(
            name for name in generated_suites if not (generated_priv / name).is_dir()
        )
        if missing_generated:
            print(
                "Registered generators with no generated directory (skipped):\n  "
                + "\n  ".join(missing_generated)
            )
        for suite_name in sorted(generated_suites - set(missing_generated)):
            suite = generated_priv / suite_name
            for source_file in sorted(
                path for path in suite.rglob("*") if path.is_file()
            ):
                link_file(source_file, generated_source, destination)
                file_count += 1
                if source_file.suffix == ".S":
                    test_count += 1
                    generated_test_count += 1

    print(
        "Staged official privileged ACT tree: "
        f"tracked_suites={len(available_tracked)} "
        f"generated_suites={len(generated_suites)} "
        f"tests={test_count} files={file_count} "
        f"generated_tests={generated_test_count} "
        f"destination={destination}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
