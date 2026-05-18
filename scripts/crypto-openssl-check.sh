#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"

if ! command -v openssl >/dev/null 2>&1; then
  echo "openssl is required for crypto verification" >&2
  exit 1
fi

work_dir=".zero/crypto-openssl"
mkdir -p "$work_dir"

crypto_test_path="$work_dir/crypto-test"
cc -std=c11 -Wall -Wextra -Wpedantic -I native/zero-c/include -DZERO_TEST \
  native/zero-c/tests/crypto/crypto_test.c \
  native/zero-c/src/crypto/ed25519.c \
  native/zero-c/src/crypto/sha256.c \
  -o "$crypto_test_path"

hex_file_write() {
  local hex_text="$1"
  local path="$2"
  python3 - <<'PY' "$hex_text" "$path"
import binascii
import sys

hex_text = sys.argv[1]
path = sys.argv[2]
data = binascii.unhexlify(hex_text) if hex_text else b""
with open(path, "wb") as handle:
    handle.write(data)
PY
}

hex_from_file() {
  local path="$1"
  python3 - <<'PY' "$path"
import binascii
import sys

path = sys.argv[1]
with open(path, "rb") as handle:
    data = handle.read()
print(binascii.hexlify(data).decode())
PY
}

openssl_sha256_hex() {
  local path="$1"
  openssl dgst -sha256 -binary "$path" | python3 -c 'import binascii, sys; print(binascii.hexlify(sys.stdin.buffer.read()).decode())'
}

sha256_check() {
  local label="$1"
  local message_hex="$2"
  local message_path="$work_dir/${label}.bin"
  hex_file_write "$message_hex" "$message_path"
  local openssl_hex
  openssl_hex="$(openssl_sha256_hex "$message_path")"
  local local_hex
  local_hex="$($crypto_test_path hash "$message_hex")"
  if [[ "$openssl_hex" != "$local_hex" ]]; then
    echo "sha256 openssl mismatch for $label" >&2
    echo "openssl: $openssl_hex" >&2
    echo "local:   $local_hex" >&2
    exit 1
  fi
}

sha256_check "empty" ""
sha256_check "abc" "616263"

message_long="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
message_long_hex="$(python3 - <<'PY' "$message_long"
import binascii
import sys

print(binascii.hexlify(sys.argv[1].encode()).decode())
PY
)"
sha256_check "long" "$message_long_hex"

seed_hex="9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
public_hex="d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
message_hex="72"

python3 - <<'PY' "$seed_hex" "$public_hex" "$work_dir"
import binascii
import sys

seed_hex = sys.argv[1]
public_hex = sys.argv[2]
work_dir = sys.argv[3]
seed = binascii.unhexlify(seed_hex)
public = binascii.unhexlify(public_hex)
alg_id = bytes.fromhex("300506032b6570")
private_key = b"\x04\x22\x04\x20" + seed
private_seq = b"\x02\x01\x00" + alg_id + private_key
private_der = b"\x30" + bytes([len(private_seq)]) + private_seq
public_key = b"\x03\x21\x00" + public
public_seq = alg_id + public_key
public_der = b"\x30" + bytes([len(public_seq)]) + public_seq

with open(f"{work_dir}/ed25519-private.der", "wb") as handle:
    handle.write(private_der)
with open(f"{work_dir}/ed25519-public.der", "wb") as handle:
    handle.write(public_der)
PY

message_path="$work_dir/ed25519-message.bin"
hex_file_write "$message_hex" "$message_path"

openssl pkeyutl -sign -inkey "$work_dir/ed25519-private.der" -keyform DER -rawin -in "$message_path" -out "$work_dir/openssl.sig"
openssl_sig_hex="$(hex_from_file "$work_dir/openssl.sig")"
"$crypto_test_path" verify "$public_hex" "$message_hex" "$openssl_sig_hex" >/dev/null

local_sig_hex="$($crypto_test_path sign "$seed_hex" "$message_hex")"
hex_file_write "$local_sig_hex" "$work_dir/local.sig"
openssl pkeyutl -verify -pubin -inkey "$work_dir/ed25519-public.der" -keyform DER -rawin -in "$message_path" -sigfile "$work_dir/local.sig" >/dev/null

echo "ok"
