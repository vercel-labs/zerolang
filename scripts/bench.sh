#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

mode="${ZERO_BENCH_MODE:-local}"
if [[ " $* " == *" --mode local "* ]]; then
  mode="local"
fi

if [[ "$mode" == "local" ]]; then
  make -C native/zero-c >/dev/null
fi

if [[ " $* " == *" --mode "* ]]; then
  scripts/bench.mts "$@"
else
  scripts/bench.mts --mode "$mode" "$@"
fi
