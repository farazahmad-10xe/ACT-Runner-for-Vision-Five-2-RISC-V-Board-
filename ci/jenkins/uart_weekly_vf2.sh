#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

export VF2_RUN_KIND="uart_weekly"
export VF2_RUN_ID_PREFIX="jenkins_uart_weekly"
export ACT_WORKDIR_NAME="work-vf2-jenkins-uart-weekly-priv"
export ACT_TEST_SCOPE="priv"
export PRIV_GENERATOR_EXTENSIONS=""
export INCLUDE_STATIC_PRIV_SUITES="true"
export EXPECTED_TEST_NAMES=""

exec "$script_dir/uart_sanity_vf2.sh" "$@"
