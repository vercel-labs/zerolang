#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

out=".zero/sanitizer/zero"
mkdir -p "$(dirname "$out")"

make -C native/zero-c \
  OUT="$root/$out" \
  CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -g -O1 -fsanitize=address,undefined"

"$out" --version >/dev/null
"$out" check examples/hello.graph >/dev/null
"$out" build --json --target linux-musl-x64 examples/hello.graph --out .zero/sanitizer/hello-linux >/dev/null
"$out" check conformance/native/pass/stdlib-target-neutral.graph >/dev/null
"$out" run conformance/native/pass/stdlib-target-neutral.graph >/dev/null
"$out" inspect --json examples/memory-package >/dev/null
"$out" size --json examples/memory-package >/dev/null
"$out" size --json examples/std-str.graph >/dev/null
"$out" mem --json examples/hello.graph >/dev/null

# Regression: dependency manifest read errors must keep an owned diagnostic
# path after the transient manifest path buffer is freed.
if [ "$(id -u)" != "0" ]; then
  dep_root="$(mktemp -d)"
  cp -R conformance/packages/dep-app "$dep_root/dep-app"
  cp -R conformance/packages/dep-lib "$dep_root/dep-lib"
  rm -rf "$dep_root/dep-app/.zero" "$dep_root/dep-lib/.zero"
  chmod 000 "$dep_root/dep-lib/zero.toml"
  dep_out="$("$out" check --json "$dep_root/dep-app" 2>&1 || true)"
  chmod 644 "$dep_root/dep-lib/zero.toml"
  rm -rf "$dep_root"
  case "$dep_out" in
    *AddressSanitizer*|*runtime\ error:*)
      echo "sanitizer error in dependency manifest read-error diagnostic path" >&2
      printf '%s\n' "$dep_out" >&2
      exit 1
      ;;
  esac
  printf '%s' "$dep_out" | grep -q "\"path\": \"$dep_root/dep-lib/zero.toml\"" || {
    echo "dependency manifest read-error diagnostic did not report the dependency manifest path" >&2
    printf '%s\n' "$dep_out" >&2
    exit 1
  }
fi
