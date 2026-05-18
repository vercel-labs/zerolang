#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

out=".zero/sanitizer/zero"
mkdir -p "$(dirname "$out")"

cc -std=c11 -Wall -Wextra -Wpedantic -g -O1 -fsanitize=address,undefined -I native/zero-c/include native/zero-c/src/*.c native/zero-c/src/crypto/*.c -o "$out"

crypto_test_path=".zero/sanitizer/crypto-test"
cc -std=c11 -Wall -Wextra -Wpedantic -g -O1 -fsanitize=address,undefined -I native/zero-c/include -DZERO_TEST \
  native/zero-c/tests/crypto/crypto_test.c \
  native/zero-c/src/crypto/ed25519.c \
  native/zero-c/src/crypto/sha256.c \
  -o "$crypto_test_path"

"$crypto_test_path" >/dev/null
"$out" --version >/dev/null
"$out" check examples/hello.0 >/dev/null
"$out" build --json --target linux-musl-x64 examples/hello.0 --out .zero/sanitizer/hello-linux >/dev/null
"$out" build --json --emit wasm --target wasm32-web examples/direct-wasm-add.0 --out .zero/sanitizer/direct-wasm-add >/dev/null
"$out" graph --json examples/memory-package >/dev/null
"$out" size --json examples/memory-package >/dev/null
"$out" mem --json examples/hello.0 >/dev/null
