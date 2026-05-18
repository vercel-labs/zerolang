#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"

if [[ -z "${ZERO_ROOT_PRIVATE_KEY:-}" ]]; then
  echo "ZERO_ROOT_PRIVATE_KEY is required" >&2
  exit 1
fi

work_dir=".zero/trusted-keys-sign"
mkdir -p "$work_dir"
key_path="$work_dir/root-key.pem"
printf '%s' "$ZERO_ROOT_PRIVATE_KEY" > "$key_path"

sig_bin="$work_dir/trusted-keys.sig.bin"
openssl pkeyutl -sign -inkey "$key_path" -rawin -in trusted-keys.json -out "$sig_bin"
xxd -p -c 256 "$sig_bin" > trusted-keys.sig

echo "trusted-keys.sig updated"
