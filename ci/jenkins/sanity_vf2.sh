#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
export REPO_ROOT="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
export RUNNER_RESOLUTION_FILE="$REPO_ROOT/.jenkins_runner_resolution.txt"

# Reuse the reviewed weekly workflow while restricting generation, Sail
# reference creation, packaging, and hardware execution to five smoke tests.
export VF2_RUN_KIND="sanity"
export VF2_RUN_ID_PREFIX="jenkins_sanity"
export ACT_WORKDIR_NAME="work-vf2-jenkins-sanity-priv"
export PRIV_GENERATOR_EXTENSIONS="ExceptionsF,ExceptionsS,ExceptionsSm,ExceptionsU,ExceptionsZc"
export INCLUDE_STATIC_PRIV_SUITES="false"
export EXPECTED_TEST_NAMES="ExceptionsF-00,ExceptionsS-00,ExceptionsSm-00,ExceptionsU-00,ExceptionsZc-00"
export HTML_REPORT_SLUG="Sanity_20Result_20Summary"

exec "$script_dir/weekly_vf2.sh" "$@"
