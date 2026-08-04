#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
export REPO_ROOT="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
export RUNNER_RESOLUTION_FILE="$REPO_ROOT/.jenkins_runner_resolution.txt"

export VF2_RUN_KIND="bpif3-weekly"
export VF2_RUN_ID_PREFIX="jenkins_bpif3_weekly"
export ACT_CONFIG_PATH="config/cores/bpif3/bpif3-rva22s64/test_config.yaml"
export ACT_DUT_YAML_PATH="config/cores/bpif3/bpif3-rva22s64/bpif3-rva22s64.yaml"
export ACT_SAIL_JSON_PATH="config/cores/bpif3/bpif3-rva22s64/sail.json"
export ACT_DUT_MACROS_PATH="config/cores/bpif3/bpif3-rva22s64/rvmodel_macros.h"
export ACT_DUT_NAME="bpif3-rva22s64"
export ACT_WORKDIR_NAME="work-bpif3-jenkins-all-priv"
export HARDWARE_BOARD="bpif3_k1"
export HARDWARE_PROFILE="ACT_PRIV_M_OWN_ENV"
export HARDWARE_PLATFORM_LABEL="BPI-F3/K1"
export HARDWARE_COLLECTOR="generic_uart"
export PRIVILEGED_FLASH_HELPER="${VF2_PRIVILEGED_HELPER_ROOT:-$REPO_ROOT}/bpif3_act_flash.sh"
export HTML_REPORT_SLUG="BPI-F3_20Result_20Summary"

exec "$script_dir/weekly_vf2.sh" "$@"
