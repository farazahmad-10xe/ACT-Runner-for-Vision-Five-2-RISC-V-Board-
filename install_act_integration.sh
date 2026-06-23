#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 /path/to/act-target/include [--with-template]"
  exit 1
fi

src_dir="$(cd "$(dirname "$0")" && pwd)/act_integration"
dst_dir="$1"
with_template="${2:-}"

mkdir -p "$dst_dir"
cp "$src_dir/rvmodel.h" "$dst_dir/rvmodel.h"

echo "Installed VF2 runner ACT adapter:"
echo "  source: $src_dir/rvmodel.h"
echo "  target: $dst_dir/rvmodel.h"

if [[ "$with_template" == "--with-template" ]]; then
  template_dir="$dst_dir/vf2_runner_target"
  mkdir -p "$template_dir"
  cp "$src_dir/template/Makefrag" "$template_dir/Makefrag"
  cp "$src_dir/template/target.env" "$template_dir/target.env"
  cp "$src_dir/template/README.md" "$template_dir/README.md"
  echo "Installed VF2 runner target skeleton:"
  echo "  target: $template_dir"
fi
