#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

make -C native/zero-c

embedded_skills_before="/tmp/zero-embedded-skills-$$.inc"
cp native/zero-c/src/embedded_skills.inc "$embedded_skills_before"
node --experimental-strip-types --disable-warning=ExperimentalWarning scripts/embed-skill-data.mts
if ! cmp -s "$embedded_skills_before" native/zero-c/src/embedded_skills.inc; then
  diff -u "$embedded_skills_before" native/zero-c/src/embedded_skills.inc
  rm -f "$embedded_skills_before"
  exit 1
fi
rm -f "$embedded_skills_before"

pnpm run check
pnpm run repository-graph:check
pnpm run rosetta:local
pnpm run stdlib:contracts
pnpm run test:zero
pnpm run extension:test

if [[ -n "${VERCEL_OIDC_TOKEN:-}" ]]; then
  ZERO_BENCH_RUNS=1 pnpm run bench:budgets
else
  ZERO_BENCH_RUNS=1 ZERO_BENCH_BUDGET=warn pnpm run bench:local
fi

bin/zero check --target linux-musl-x64 examples/memory-package
bin/zero build --emit obj --target linux-musl-x64 examples/direct-obj-add.graph --out .zero/out/direct-obj-add-linux-musl.o
bin/zero build --target linux-musl-x64 examples/hello.graph --out .zero/out/hello-linux-musl
bin/zero build --target win32-x64.exe examples/hello.graph --out .zero/out/hello-win32

mkdir -p .zero/ci-release
node --experimental-strip-types --disable-warning=ExperimentalWarning scripts/embed-skill-data.mts
build_hash="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
ZIG_GLOBAL_CACHE_DIR=.zero/zig-global-cache ZIG_LOCAL_CACHE_DIR=.zero/zig-local-cache zig cc -target x86_64-linux-musl -std=c11 -D_POSIX_C_SOURCE=200809L -DZERO_BUILD_HASH="\"$build_hash\"" -Os -Inative/zero-c/include native/zero-c/src/*.c -o .zero/ci-release/zero-linux-musl-x64
test -s .zero/ci-release/zero-linux-musl-x64
if [[ "$(uname -s):$(uname -m)" == "Linux:x86_64" ]]; then
  ./.zero/ci-release/zero-linux-musl-x64 --version
  ./.zero/ci-release/zero-linux-musl-x64 skills list --json
fi
