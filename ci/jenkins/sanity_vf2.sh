#!/usr/bin/env bash
set -euo pipefail

# Reuse the reviewed weekly workflow while restricting generation, Sail
# reference creation, packaging, and hardware execution to five smoke tests.
export VF2_RUN_KIND="sanity"
export VF2_RUN_ID_PREFIX="jenkins_sanity"
export ACT_WORKDIR_NAME="work-vf2-jenkins-sanity-priv"
export PRIV_GENERATOR_EXTENSIONS="ExceptionsF,ExceptionsS,ExceptionsSm,ExceptionsU,ExceptionsZc"
export INCLUDE_STATIC_PRIV_SUITES="false"
export EXPECTED_TEST_NAMES="ExceptionsF-00,ExceptionsS-00,ExceptionsSm-00,ExceptionsU-00,ExceptionsZc-00"

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
exec "$script_dir/weekly_vf2.sh" "$@"
