#!/usr/bin/env bash
set -euo pipefail

if [[ "${CI:-}" == "true" || "${ZERO_NATIVE_TEST_TRACE:-}" == "1" ]]; then
  PS4='+ ${BASH_SOURCE[0]}:${LINENO}: '
  set -x
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if [[ "${ZERO_NATIVE_TEST_SANDBOX:-}" != "1" && "${ZERO_NATIVE_TEST_ALLOW_LOCAL:-}" != "1" ]]; then
  echo "native tests emit native artifacts; run 'pnpm run native:test' for Vercel Sandbox execution or set ZERO_NATIVE_TEST_ALLOW_LOCAL=1 to opt into local artifacts." >&2
  exit 1
fi

make -C native/zero-c

mkdir -p .zero/native-test .zero/conformance

host_runtime_target=""
case "$(uname -s):$(uname -m)" in
  Darwin:arm64) host_runtime_target="darwin-arm64" ;;
  Linux:x86_64) host_runtime_target="linux-x64" ;;
esac

if [[ -n "$host_runtime_target" ]] && command -v cc >/dev/null 2>&1; then
  runtime_cwd_dir="/tmp/zero-runtime-cwd-$$"
  runtime_cwd_out="$runtime_cwd_dir/std-json-bytes"
  rm -rf "$runtime_cwd_dir"
  mkdir -p "$runtime_cwd_dir"
  (
    cd "$runtime_cwd_dir"
    "$root/.zero/bin/zero" build --json --emit exe --target "$host_runtime_target" "$root/conformance/native/pass/std-json-bytes.0" --out "$runtime_cwd_out" > "$runtime_cwd_out.json"
  )
  set +e
  "$runtime_cwd_out"
  runtime_cwd_status=$?
  set -e
  test "$runtime_cwd_status" = "0"
  test ! -f "$runtime_cwd_out.zero.o"
  test ! -f "$runtime_cwd_out.zero-runtime.o"
  test ! -f "$runtime_cwd_out.zero-runtime.o.zero_runtime.c"
  test ! -e "$runtime_cwd_out.zero-runtime.o.include"
  node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(process.argv[1],"utf8")); if (report.generatedCBytes!==0 || report.objectBackend.linking.targetLibraries!=="zero-runtime" || report.objectBackend.linking.externalToolchain!=="cc" || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_runtime.o") || report.objectBackend.directFacts.runtimeHelperCount!==1) process.exit(1);' "$runtime_cwd_out.json"
  set +e
  ZERO_CC=/usr/bin/false "$root/.zero/bin/zero" build --json --emit exe --target "$host_runtime_target" "$root/conformance/native/pass/std-json-bytes.0" --out "$runtime_cwd_out-bad" > "$runtime_cwd_out-bad.json" 2>/dev/null
  runtime_zero_cc_status=$?
  set -e
  test "$runtime_zero_cc_status" != "0"
  grep -q "host runtime object build failed" "$runtime_cwd_out-bad.json"
  rm -rf "$runtime_cwd_dir"
fi

if command -v zig >/dev/null 2>&1; then
  json_cross_out=".zero/native-test/std-json-bytes-linux-musl"
  rm -f "$json_cross_out" "$json_cross_out.json" "$json_cross_out.zero.o" "$json_cross_out.zero-runtime.o"
  bin/zero build --json --emit exe --target linux-musl-x64 conformance/native/pass/std-json-bytes.0 --out "$json_cross_out" > "$json_cross_out.json"
  node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(process.argv[1],"utf8")); if (report.generatedCBytes!==0 || report.target!=="linux-musl-x64" || report.objectBackend.linking.targetLibraries!=="zero-runtime" || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_runtime.o")) process.exit(1);' "$json_cross_out.json"
  test ! -f "$json_cross_out.zero.o"
  test ! -f "$json_cross_out.zero-runtime.o"
fi

expected_output() {
  case "$1" in
    examples/hello.0) printf "hello from zero" ;;
    examples/hello-let.0) printf "hello from a binding" ;;
    examples/add.0) printf "math works" ;;
    examples/branch.0) printf "branch yes" ;;
    examples/countdown.0) printf "countdown done" ;;
    examples/functions.0) printf "function call works" ;;
    examples/point.0) printf "point works" ;;
    examples/systems-package) printf "systems package" ;;
    examples/readall-cli) printf "readall cli ok" ;;
    examples/batch3-cli) printf "batch3 cli ok" ;;
    examples/resource-cli) printf "resource cli ok" ;;
    examples/zero-hash) printf "zero-hash ok" ;;
    examples/memory-package) printf "memory package ok" ;;
    examples/result-choice.0) printf "choice ok" ;;
    examples/const-arithmetic.0) printf "const arithmetic ok" ;;
    examples/generic-pair.0) printf "generic pair ok" ;;
    examples/type-alias.0) printf "type alias ok" ;;
    examples/static-method.0) printf "static method ok" ;;
    examples/static-interface.0) printf "static interface ok" ;;
    examples/fallibility.0) printf "fallibility ok" ;;
    examples/ownership-cleanup.0) printf "ownership cleanup ok" ;;
    examples/codec-varint.0) printf "codec primitives ok" ;;
    examples/parse-cursor.0) printf "parse primitives ok" ;;
    examples/file-copy.0) printf "file copy ok" ;;
    conformance/native/pass/std-crypto-hmac32.0) printf "crypto hmac32 ok" ;;
    conformance/native/pass/string-byte-ergonomics.0) printf "string byte ergonomics ok" ;;
    *)
      echo "missing expected output for $1" >&2
      exit 1
      ;;
  esac
}

examples=(
  examples/hello.0
  examples/hello-let.0
  examples/add.0
  examples/branch.0
  examples/countdown.0
  examples/functions.0
  examples/point.0
  examples/systems-package
  examples/readall-cli
  examples/batch3-cli
  examples/resource-cli
  examples/zero-hash
  examples/memory-package
  examples/result-choice.0
  examples/const-arithmetic.0
  examples/generic-pair.0
  examples/type-alias.0
  examples/static-method.0
  examples/static-interface.0
  examples/fallibility.0
  examples/ownership-cleanup.0
  examples/codec-varint.0
  examples/parse-cursor.0
  examples/file-copy.0
  conformance/native/pass/std-crypto-hmac32.0
  conformance/native/pass/string-byte-ergonomics.0
)

run_native_or_gap() {
  local input="$1"
  local out="$2"
  local expected="$3"
  shift 3

  bin/zero check "$input" >/dev/null
  if bin/zero build --json --emit exe --target linux-musl-x64 "$input" --out "$out" > "$out.json"; then
    local native_output
    if ! native_output="$("$out" "$@" 2>/dev/null)"; then
      return 0
    fi
    if [[ "$native_output" != "$expected" ]]; then
      echo "native output mismatch for $input" >&2
      echo "native:   $native_output" >&2
      echo "expected: $expected" >&2
      exit 1
    fi
  else
    grep -q '"code"[[:space:]]*:[[:space:]]*"CGEN004"' "$out.json"
  fi
}

for example in "${examples[@]}"; do
  name="$(basename "$example" .0)"
  native_exe=".zero/native-test/$name"
  run_native_or_gap "$example" "$native_exe" "$(expected_output "$example")"
done

project=".zero/native-test/project"
mkdir -p "$project/src"
cat > "$project/zero.json" <<'PROJECT'
{
  "package": { "name": "native-project", "version": "0.1.0" },
  "targets": { "cli": { "kind": "exe", "main": "src/main.0" } }
}
PROJECT

cat > "$project/src/main.0" <<'SOURCE'
pub fn main Void world World !
  check world.out.write "native project\n"
SOURCE

run_native_or_gap "$project" .zero/native-test/project-exe "native project"
run_native_or_gap conformance/native/pass/world-stream-renamed-param.0 .zero/native-test/world-stream-renamed-param "renamed world"

run_native_or_gap conformance/native/pass/params.0 .zero/native-test/params "params work"
run_native_or_gap conformance/native/pass/shape.0 .zero/native-test/shape "native shape"
run_native_or_gap conformance/native/pass/primitive-stdlib.0 .zero/native-test/primitive-stdlib "native primitive std"
run_native_or_gap conformance/native/pass/variants-defer-stdlib.0 .zero/native-test/variants-defer-stdlib "native variants std"
run_native_or_gap conformance/native/pass/payload-match.0 .zero/native-test/payload-match "native payload match"
run_native_or_gap conformance/native/pass/std-mem-arrays.0 .zero/native-test/std-mem-arrays "native std mem arrays"
run_native_or_gap conformance/native/pass/memory-types.0 .zero/native-test/memory-types "native memory types"
run_native_or_gap conformance/native/pass/recursive-fibonacci.0 .zero/native-test/recursive-fibonacci "recursive fibonacci ok"
run_native_or_gap conformance/native/pass/scratch-nested-index.0 .zero/native-test/scratch-nested-index "scratch nested index ok"
run_native_or_gap conformance/native/pass/owned-transfer.0 .zero/native-test/owned-transfer "owned transfer ok"
run_native_or_gap conformance/native/pass/owned-drop-cleanup.0 .zero/native-test/owned-drop-cleanup "owned drop cleanup ok"
run_native_or_gap conformance/native/pass/owned-drop-move-suppressed.0 .zero/native-test/owned-drop-move-suppressed "owned drop move suppressed ok"
run_native_or_gap conformance/native/pass/borrow-primitives.0 .zero/native-test/borrow-primitives "borrow primitives ok"
run_native_or_gap conformance/native/pass/mutref-indexed-lvalues.0 .zero/native-test/mutref-indexed-lvalues "mutref indexed lvalues ok"
run_native_or_gap conformance/native/pass/allocator-primitives.0 .zero/native-test/allocator-primitives "allocator primitives ok"
run_native_or_gap conformance/native/pass/owned-byte-buffer.0 .zero/native-test/owned-byte-buffer "owned byte buffer ok"
run_native_or_gap conformance/native/pass/std-mem-arena.0 .zero/native-test/std-mem-arena "arena ok"
run_native_or_gap conformance/native/pass/std-json-duplicate-keys.0 .zero/native-test/std-json-duplicate-keys ""
run_native_or_gap conformance/native/pass/std-json-allocator-capacity.0 .zero/native-test/std-json-allocator-capacity ""
run_native_or_gap conformance/native/pass/fallibility-error-sets.0 .zero/native-test/fallibility-error-sets "fallibility error sets ok"
run_native_or_gap conformance/native/pass/rescue-check.0 .zero/native-test/rescue-check "rescue ok"
run_native_or_gap conformance/native/pass/std-fs-fallible.0 .zero/native-test/std-fs-fallible "fs named errors ok"
run_native_or_gap conformance/native/pass/std-fs-fallible-resources.0 .zero/native-test/std-fs-fallible-resources "fs fallible resources ok"
run_native_or_gap conformance/native/pass/std-cli-helpers.0 .zero/native-test/std-cli-helpers "cli helpers ok"
std_args_run_exe="/tmp/zero-std-args-run-$$"
std_args_run_output="$(bin/zero run --out "$std_args_run_exe" conformance/native/pass/std-args.0 -- agent-arg extra)"
rm -f "$std_args_run_exe"
if [[ "$std_args_run_output" != "agent-arg" ]]; then
  echo "zero run output mismatch for conformance/native/pass/std-args.0" >&2
  echo "native:   $std_args_run_output" >&2
  echo "expected: agent-arg" >&2
  exit 1
fi
bin/zero build --json --emit exe --target linux-musl-x64 conformance/native/pass/std-args.0 --out .zero/native-test/std-args > .zero/native-test/std-args.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/std-args.json
if [ "$(uname -s)" = "Linux" ] && [ "$(uname -m)" = "x86_64" ]; then
  std_args_output="$(.zero/native-test/std-args agent-arg extra)"
  if [[ "$std_args_output" != "agent-arg" ]]; then
    echo "native output mismatch for conformance/native/pass/std-args.0" >&2
    echo "native:   $std_args_output" >&2
    echo "expected: agent-arg" >&2
    exit 1
  fi
  rm -f .zero/native-test/std-http-response-helpers-linux .zero/native-test/std-http-response-helpers-linux.json .zero/native-test/std-http-response-helpers-linux.zero.o .zero/native-test/std-http-response-helpers-linux.zero-runtime.o
  if ! bin/zero build --json --emit exe --target linux-x64 conformance/native/pass/std-http-response-helpers.0 --out .zero/native-test/std-http-response-helpers-linux > .zero/native-test/std-http-response-helpers-linux.json; then
    cat .zero/native-test/std-http-response-helpers-linux.json >&2
    exit 1
  fi
  set +e
  .zero/native-test/std-http-response-helpers-linux
  std_http_response_helpers_linux_status=$?
  set -e
  test "$std_http_response_helpers_linux_status" = "29"
  test ! -f .zero/native-test/std-http-response-helpers-linux.zero.o
  test ! -f .zero/native-test/std-http-response-helpers-linux.zero-runtime.o
  node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/std-http-response-helpers-linux.json","utf8")); if (report.generatedCBytes!==0 || report.objectBackend.objectEmission.path!=="direct-elf64-object" || report.objectBackend.linking.targetLibraries!=="zero-runtime" || report.objectBackend.linking.externalToolchain!=="cc" || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_runtime.o") || report.objectBackend.directFacts.runtimeHelperCount!==1) process.exit(1);'
  curl_link_smoke_src="/tmp/zero-curl-link-smoke-$$.c"
  curl_link_smoke_exe="/tmp/zero-curl-link-smoke-$$"
  cat > "$curl_link_smoke_src" <<'SOURCE'
#include <curl/curl.h>
int main(void) {
  return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 0 : 1;
}
SOURCE
  if cc "$curl_link_smoke_src" -lcurl -o "$curl_link_smoke_exe" >/dev/null 2>&1; then
    rm -f .zero/native-test/std-http-fetch-linux .zero/native-test/std-http-fetch-linux.json .zero/native-test/std-http-fetch-linux.zero.o .zero/native-test/std-http-fetch-linux.zero-runtime.o .zero/native-test/std-http-fetch-linux.zero-http-curl.o
    if ! bin/zero build --json --emit exe --target linux-x64 conformance/native/pass/std-http-fetch.0 --out .zero/native-test/std-http-fetch-linux > .zero/native-test/std-http-fetch-linux.json; then
      cat .zero/native-test/std-http-fetch-linux.json >&2
      exit 1
    fi
    set +e
    .zero/native-test/std-http-fetch-linux
    std_http_fetch_linux_status=$?
    set -e
    test "$std_http_fetch_linux_status" = "23"
    test ! -f .zero/native-test/std-http-fetch-linux.zero.o
    test ! -f .zero/native-test/std-http-fetch-linux.zero-runtime.o
    test ! -f .zero/native-test/std-http-fetch-linux.zero-http-curl.o
    node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/std-http-fetch-linux.json","utf8")); if (report.generatedCBytes!==0 || report.objectBackend.objectEmission.path!=="direct-elf64-object" || report.objectBackend.linking.targetLibraries!=="zero-runtime,curl" || report.objectBackend.linking.externalToolchain!=="cc" || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_runtime.o") || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_http_curl.o") || !report.objectBackend.linkerPlan.systemLibraries.includes("curl") || report.objectBackend.directFacts.runtimeHelperCount!==2 || !(report.objectBackend.directFacts.runtime.readonlyDataBytes > 0)) process.exit(1);'
    scripts/http-runtime-smoke.mts
  fi
  rm -f "$curl_link_smoke_src" "$curl_link_smoke_exe"
fi

bin/zero check conformance/native/pass/std-env.0 >/dev/null

run_native_or_gap conformance/native/pass/std-fs-bytes.0 .zero/native-test/std-fs-bytes "fs bytes ok"

run_native_or_gap conformance/native/pass/std-fs-resource.0 .zero/native-test/std-fs-resource "fs resource ok"

run_native_or_gap conformance/native/pass/std-fs-readall.0 .zero/native-test/std-fs-readall "fs readAll ok"

run_native_or_gap conformance/native/pass/std-fs-polish.0 .zero/native-test/std-fs-polish "fs polish ok"

run_native_or_gap conformance/native/pass/std-mem-copy-fill.0 .zero/native-test/std-mem-copy-fill "mem copy fill ok"

run_native_or_gap conformance/native/pass/generic-function-basic.0 .zero/native-test/generic-function-basic "generic function ok"

run_native_or_gap conformance/native/pass/generic-shape-basic.0 .zero/native-test/generic-shape-basic "generic shape ok"

run_native_or_gap conformance/native/pass/generic-shape-multi.0 .zero/native-test/generic-shape-multi "generic shape multi ok"

run_native_or_gap conformance/native/pass/generic-constructor-expected.0 .zero/native-test/generic-constructor-expected "generic constructor expected ok"

run_native_or_gap conformance/native/pass/generic-literals-arrays.0 .zero/native-test/generic-literals-arrays "generic literals arrays ok"

run_native_or_gap conformance/native/pass/top-level-const.0 .zero/native-test/top-level-const "const ok"

run_native_or_gap conformance/native/pass/const-arithmetic.0 .zero/native-test/const-arithmetic "const arithmetic ok"

run_native_or_gap conformance/native/pass/type-alias-basic.0 .zero/native-test/type-alias-basic "type alias ok"

run_native_or_gap conformance/native/pass/static-method-namespace.0 .zero/native-test/static-method-namespace "static method ok"

run_native_or_gap conformance/native/pass/match-fallback.0 .zero/native-test/match-fallback "match fallback ok"

bin/zero graph --json conformance/check/pass/imports > .zero/native-test/imports-graph.json
grep -q '"imports": \["math", "types"\]' .zero/native-test/imports-graph.json
grep -q '"targets":' .zero/native-test/imports-graph.json
bin/zero graph --json examples/resource-cli > .zero/native-test/resource-cli-graph.json
grep -q '"importEdges":' .zero/native-test/resource-cli-graph.json
grep -q '"requiresCapabilities": \["args", "env", "fs", "memory", "path", "world"\]' .zero/native-test/resource-cli-graph.json
bin/zero graph --json conformance/native/pass/std-io-direct.0 > .zero/native-test/std-io-direct-graph.json
grep -q '"requiresCapabilities": \["memory", "world"\]' .zero/native-test/std-io-direct-graph.json
grep -q '"name":"std.io.bufferedReader"' .zero/native-test/std-io-direct-graph.json
grep -q '"name":"std.io.bufferedWriter"' .zero/native-test/std-io-direct-graph.json
grep -q '"name":"std.io.copy"' .zero/native-test/std-io-direct-graph.json
bin/zero graph --json examples/static-method.0 > .zero/native-test/static-method-graph.json
grep -q '"methods":' .zero/native-test/static-method-graph.json
grep -q '"staticDispatch":true' .zero/native-test/static-method-graph.json
bin/zero graph --json examples/type-alias.0 > .zero/native-test/type-alias-graph.json
grep -q '"aliases":' .zero/native-test/type-alias-graph.json
grep -q '"kind":"type-alias"' .zero/native-test/type-alias-graph.json
bin/zero graph --json --target linux-musl-x64 examples/memory-package > .zero/native-test/memory-package-graph.json
grep -q '"fsAvailable":true' .zero/native-test/memory-package-graph.json
grep -q '"requiresCapabilities": \["memory", "world"\]' .zero/native-test/memory-package-graph.json
bin/zero size --json conformance/native/pass/std-io-direct.0 > .zero/native-test/std-io-direct-size.json
grep -q '"name":"std.io.bufferedReader"' .zero/native-test/std-io-direct-size.json
grep -q '"name":"std.io.bufferedWriter"' .zero/native-test/std-io-direct-size.json
grep -q '"name":"std.io.copy"' .zero/native-test/std-io-direct-size.json
grep -q '"name":"stdio-world"' .zero/native-test/std-io-direct-size.json
bin/zero mem --json conformance/native/pass/std-io-direct.0 > .zero/native-test/std-io-direct-mem.json
grep -q '"generatedCBytes": 0' .zero/native-test/std-io-direct-mem.json
grep -q '"cBridgeFallback": false' .zero/native-test/std-io-direct-mem.json
grep -q '"stackBytes":' .zero/native-test/std-io-direct-mem.json
grep -q '"maxFrameBytes":' .zero/native-test/std-io-direct-mem.json
grep -q '"readonlyDataBytes":' .zero/native-test/std-io-direct-mem.json
grep -q '"runtimeHelperCount":1' .zero/native-test/std-io-direct-mem.json
grep -q '"name":"std.io.bufferedReader"' .zero/native-test/std-io-direct-mem.json
grep -q '"name":"std.io.bufferedWriter"' .zero/native-test/std-io-direct-mem.json
grep -q '"name":"std.io.copy"' .zero/native-test/std-io-direct-mem.json
grep -q '"hiddenHeapAllocation":false' .zero/native-test/std-io-direct-mem.json
bin/zero check --target linux-musl-x64 conformance/native/pass/std-http-metadata-neutral.0 >/dev/null
bin/zero graph --json --target linux-musl-x64 conformance/native/pass/std-http-metadata-neutral.0 > .zero/native-test/std-http-metadata-neutral-graph.json
grep -q '"requiresCapabilities": \["memory", "parse"\]' .zero/native-test/std-http-metadata-neutral-graph.json
if bin/zero check --target linux-musl-x64 conformance/native/pass/std-http-fetch.0 >/dev/null 2>.zero/native-test/std-http-target-unsupported.err; then
  echo "expected hosted std.http to fail on target without net" >&2
  exit 1
fi
grep -q "TAR002" .zero/native-test/std-http-target-unsupported.err
bin/zero tokens --json conformance/lexer/compiler-smoke.0 > .zero/native-test/lexer-tokens.json
node <<'NODE'
const assert = require("node:assert/strict");
const fs = require("node:fs");

const body = JSON.parse(fs.readFileSync(".zero/native-test/lexer-tokens.json", "utf8"));
assert.equal(body.schemaVersion, 1);
assert.equal(body.syntax, "row");
assert.deepEqual(body.tokens.slice(0, 4).map((token) => `${token.kind}:${token.text}`), [
  "word:use",
  "word:std",
  "symbol:.",
  "word:mem",
]);
assert.deepEqual(body.tokens.slice(5, 9).map((token) => `${token.kind}:${token.text}`), [
  "word:pub",
  "word:fn",
  "word:main",
  "word:Void",
]);
assert.equal(body.tokens[5].offset, 12);
assert.equal(body.tokens[12].line, 3);
assert.equal(body.tokens.at(-1).kind, "eof");
NODE

bin/zero parse --json conformance/parse/compiler-smoke.0 > .zero/native-test/parse-tree.json
node <<'NODE'
const assert = require("node:assert/strict");
const fs = require("node:fs");

const body = JSON.parse(fs.readFileSync(".zero/native-test/parse-tree.json", "utf8"));
assert.equal(body.schemaVersion, 1);
assert.equal(body.root.kind, "module");
assert.equal(body.root.shapeCount, 1);
assert.equal(body.root.enumCount, 1);
assert.equal(body.root.choiceCount, 1);
assert.equal(body.root.functionCount, 1);
assert.deepEqual(body.functions[0].bodyKinds, ["if", "while", "check", "return"]);
NODE

assert_direct_size_metadata() {
  local fixture="$1"
  local out_json=".zero/native-test/size-metadata-check.json"
  bin/zero size --json "$fixture" > "$out_json"
  node -e '
    const fs = require("node:fs");
    const fixture = process.argv[1];
    const body = JSON.parse(fs.readFileSync(".zero/native-test/size-metadata-check.json", "utf8"));
    const hasDirectSizeMetadata = Array.isArray(body.sections) && body.sections.some((section) => section.name === "direct-size-metadata");
    const hasProfileSizeMetadata = body.sizeBreakdown && body.profileBudget && body.profileSemantics;
    if (body.generatedCBytes !== 0 || !(body.loweredIrBytes > 0) || !hasDirectSizeMetadata || !hasProfileSizeMetadata) {
      console.error(`unexpected direct size metadata for ${fixture}: ${JSON.stringify({
        generatedCBytes: body.generatedCBytes,
        loweredIrBytes: body.loweredIrBytes,
        hasDirectSizeMetadata,
        hasProfileSizeMetadata,
      })}`);
      process.exit(1);
    }
  ' "$fixture"
}

assert_direct_size_metadata "conformance/native/pass/std-fs-readall.0"
for size_case in \
  "examples/hello.0" \
  "examples/resource-cli" \
  "examples/zero-hash" \
  "examples/const-arithmetic.0" \
  "examples/generic-pair.0" \
  "examples/type-alias.0" \
  "examples/static-method.0" \
  "conformance/native/pass/generic-function-basic.0" \
  "conformance/native/pass/generic-shape-basic.0" \
  "conformance/native/pass/generic-shape-multi.0" \
  "conformance/native/pass/top-level-const.0" \
  "conformance/native/pass/const-arithmetic.0" \
  "conformance/native/pass/type-alias-basic.0" \
  "conformance/native/pass/static-method-namespace.0" \
  "conformance/native/pass/match-fallback.0" \
  "conformance/native/pass/std-mem-copy-fill.0"
do
  assert_direct_size_metadata "$size_case"
done
bin/zero size --json examples/hello.0 > .zero/native-test/hello-size.json
node -e 'const fs=require("node:fs"); const j=JSON.parse(fs.readFileSync(".zero/native-test/hello-size.json","utf8")); if (!j.compilerRuntimeHelpers.every((h)=>h.payAsUsed===true && h.emitted===false)) process.exit(1);'

[[ "$(bin/zero test conformance/native/pass/test-blocks.0)" == "1 test(s) ok" ]]
[[ "$(bin/zero run --out .zero/native-test/run-add examples/add.0)" == "math works" ]]
bin/zero test --json conformance/packages/test-app > .zero/native-test/test-package.json
node -e 'const fs=require("node:fs"); const j=JSON.parse(fs.readFileSync(".zero/native-test/test-package.json","utf8")); if (!j.ok || j.testDiscovery.mode!=="package" || j.discoveredTests!==3 || j.selectedTests!==3 || j.expectedFailures!==1 || !j.fixtures.sourceFiles.some((p)=>p.endsWith("helper.0")) || !j.targetFacts.capabilitySupport) process.exit(1);'
bin/zero test --json --filter helper conformance/packages/test-app > .zero/native-test/test-package-filter.json
node -e 'const fs=require("node:fs"); const j=JSON.parse(fs.readFileSync(".zero/native-test/test-package-filter.json","utf8")); if (!j.ok || j.discoveredTests!==3 || j.selectedTests!==2 || j.testDiscovery.filter!=="helper") process.exit(1);'
bin/zero test --json conformance/native/pass/test-expected-fail.0 > .zero/native-test/test-expected-fail.json
node -e 'const fs=require("node:fs"); const j=JSON.parse(fs.readFileSync(".zero/native-test/test-expected-fail.json","utf8")); if (!j.ok || j.expectedFailures!==1 || j.failedTests!==0 || j.results[0].status!=="expected-fail") process.exit(1);'
if bin/zero test --json conformance/native/fail/test-unexpected-pass.0 > .zero/native-test/test-unexpected-pass.json; then
  echo "expected unexpected-pass fixture to fail" >&2
  exit 1
fi
node -e 'const fs=require("node:fs"); const j=JSON.parse(fs.readFileSync(".zero/native-test/test-unexpected-pass.json","utf8")); if (j.ok || j.unexpectedPasses!==1 || j.results[0].status!=="unexpected-pass") process.exit(1);'
scripts/reliability-smoke.mts >/dev/null
if bin/zero test conformance/native/fail/test-expect-runtime-fail.0 >/dev/null 2>.zero/native-test/test-expect-runtime-fail.err; then
  echo "expected failing test block to fail" >&2
  exit 1
fi
grep -q "zero test expectation failed" .zero/native-test/test-expect-runtime-fail.err
grep -q "expect runtime failure exits nonzero" .zero/native-test/test-expect-runtime-fail.err

if bin/zero check conformance/native/fail/wrong-arity.0 2>.zero/native-test/wrong-arity.err; then
  echo "expected wrong-arity fixture to fail" >&2
  exit 1
fi
grep -q "NAM004" .zero/native-test/wrong-arity.err

if bin/zero check conformance/native/fail/bad-return.0 2>.zero/native-test/bad-return.err; then
  echo "expected bad-return fixture to fail" >&2
  exit 1
fi
grep -q "TYP003" .zero/native-test/bad-return.err

if bin/zero check conformance/native/fail/duplicate-function.0 2>.zero/native-test/duplicate-function.err; then
  echo "expected duplicate-function fixture to fail" >&2
  exit 1
fi
grep -q "NAM004" .zero/native-test/duplicate-function.err

if bin/zero check conformance/native/fail/unknown-field.0 2>.zero/native-test/unknown-field.err; then
  echo "expected unknown-field fixture to fail" >&2
  exit 1
fi
grep -q "FLD001" .zero/native-test/unknown-field.err

if bin/zero check conformance/native/fail/immutable-assignment.0 2>.zero/native-test/immutable-assignment.err; then
  echo "expected immutable-assignment fixture to fail" >&2
  exit 1
fi
grep -q "TYP009" .zero/native-test/immutable-assignment.err

if bin/zero check conformance/native/fail/bad-std-call.0 2>.zero/native-test/bad-std-call.err; then
  echo "expected bad-std-call fixture to fail" >&2
  exit 1
fi
grep -q "STD002" .zero/native-test/bad-std-call.err

if bin/zero check conformance/native/fail/fs-open-without-capability.0 2>.zero/native-test/fs-open-without-capability.err; then
  echo "expected fs-open-without-capability fixture to fail" >&2
  exit 1
fi
grep -q "STD003" .zero/native-test/fs-open-without-capability.err

if bin/zero check conformance/native/fail/fs-read-without-mutref.0 2>.zero/native-test/fs-read-without-mutref.err; then
  echo "expected fs-read-without-mutref fixture to fail" >&2
  exit 1
fi
grep -q "STD003" .zero/native-test/fs-read-without-mutref.err

if bin/zero check conformance/native/fail/mem-copy-immutable-dst.0 2>.zero/native-test/mem-copy-immutable-dst.err; then
  echo "expected mem-copy-immutable-dst fixture to fail" >&2
  exit 1
fi
grep -q "TYP009" .zero/native-test/mem-copy-immutable-dst.err

if bin/zero check conformance/native/fail/std-fs-create-error-set-mismatch.0 2>.zero/native-test/std-fs-create-error-set-mismatch.err; then
  echo "expected std-fs-create-error-set-mismatch fixture to fail" >&2
  exit 1
fi
grep -q "ERR002" .zero/native-test/std-fs-create-error-set-mismatch.err

if bin/zero check conformance/native/fail/std-fs-unchecked-resource-fallible.0 2>.zero/native-test/std-fs-unchecked-resource-fallible.err; then
  echo "expected std-fs-unchecked-resource-fallible fixture to fail" >&2
  exit 1
fi
grep -q "ERR003" .zero/native-test/std-fs-unchecked-resource-fallible.err

if bin/zero check conformance/native/fail/bad-memory-type.0 2>.zero/native-test/bad-memory-type.err; then
  echo "expected bad-memory-type fixture to fail" >&2
  exit 1
fi
grep -q "MEM001" .zero/native-test/bad-memory-type.err

if bin/zero check conformance/native/fail/nonexhaustive-match.0 2>.zero/native-test/nonexhaustive-match.err; then
  echo "expected nonexhaustive-match fixture to fail" >&2
  exit 1
fi
grep -q "MAT002" .zero/native-test/nonexhaustive-match.err

if bin/zero check conformance/native/fail/bad-choice-payload.0 2>.zero/native-test/bad-choice-payload.err; then
  echo "expected bad-choice-payload fixture to fail" >&2
  exit 1
fi
grep -q "VAR004" .zero/native-test/bad-choice-payload.err

if bin/zero check conformance/native/fail/allocator-invalid.0 2>.zero/native-test/allocator-invalid.err; then
  echo "expected allocator-invalid fixture to fail" >&2
  exit 1
fi
grep -q "STD003" .zero/native-test/allocator-invalid.err

if bin/zero check conformance/native/fail/allocator-immutable-fixedbuf.0 2>.zero/native-test/allocator-immutable-fixedbuf.err; then
  echo "expected allocator-immutable-fixedbuf fixture to fail" >&2
  exit 1
fi
grep -q "STD003" .zero/native-test/allocator-immutable-fixedbuf.err

if bin/zero check conformance/native/fail/std-json-parsebytes-raw-alloc.0 2>.zero/native-test/std-json-parsebytes-raw-alloc.err; then
  echo "expected std-json-parsebytes-raw-alloc fixture to fail" >&2
  exit 1
fi
grep -q "STD003" .zero/native-test/std-json-parsebytes-raw-alloc.err

if bin/zero check conformance/native/fail/std-json-parsebytes-immutable-alloc.0 2>.zero/native-test/std-json-parsebytes-immutable-alloc.err; then
  echo "expected std-json-parsebytes-immutable-alloc fixture to fail" >&2
  exit 1
fi
grep -q "STD003" .zero/native-test/std-json-parsebytes-immutable-alloc.err

if bin/zero check conformance/native/fail/owned-use-after-move.0 2>.zero/native-test/owned-use-after-move.err; then
  echo "expected owned-use-after-move fixture to fail" >&2
  exit 1
fi
grep -q "OWN001" .zero/native-test/owned-use-after-move.err

if bin/zero check conformance/native/fail/invalid-drop-signature.0 2>.zero/native-test/invalid-drop-signature.err; then
  echo "expected invalid-drop-signature fixture to fail" >&2
  exit 1
fi
grep -q "OWN002" .zero/native-test/invalid-drop-signature.err

if bin/zero check conformance/native/fail/borrow-mutref-immutable.0 2>.zero/native-test/borrow-mutref-immutable.err; then
  echo "expected borrow-mutref-immutable fixture to fail" >&2
  exit 1
fi
grep -q "TYP009" .zero/native-test/borrow-mutref-immutable.err

if bin/zero check conformance/native/fail/borrow-conflict.0 2>.zero/native-test/borrow-conflict.err; then
  echo "expected borrow-conflict fixture to fail" >&2
  exit 1
fi
grep -q "BOR001" .zero/native-test/borrow-conflict.err

if bin/zero check conformance/native/fail/borrow-assign-while-borrowed.0 2>.zero/native-test/borrow-assign-while-borrowed.err; then
  echo "expected borrow-assign-while-borrowed fixture to fail" >&2
  exit 1
fi
grep -q "BOR001" .zero/native-test/borrow-assign-while-borrowed.err

if bin/zero check conformance/native/fail/borrow-assign-through-ref.0 2>.zero/native-test/borrow-assign-through-ref.err; then
  echo "expected borrow-assign-through-ref fixture to fail" >&2
  exit 1
fi
grep -q "TYP009" .zero/native-test/borrow-assign-through-ref.err

if bin/zero check conformance/native/fail/borrow-return-local.0 2>.zero/native-test/borrow-return-local.err; then
  echo "expected borrow-return-local fixture to fail" >&2
  exit 1
fi
grep -q "BOR002" .zero/native-test/borrow-return-local.err

if bin/zero check conformance/native/fail/borrow-wrong-type.0 2>.zero/native-test/borrow-wrong-type.err; then
  echo "expected borrow-wrong-type fixture to fail" >&2
  exit 1
fi
grep -q "TYP001" .zero/native-test/borrow-wrong-type.err

if bin/zero check conformance/native/fail/ref-indexed-assignment.0 2>.zero/native-test/ref-indexed-assignment.err; then
  echo "expected ref-indexed-assignment fixture to fail" >&2
  exit 1
fi
grep -q "TYP009" .zero/native-test/ref-indexed-assignment.err

if bin/zero check conformance/native/fail/check-maybe-void.0 2>.zero/native-test/check-maybe-void.err; then
  echo "expected check-maybe-void fixture to fail" >&2
  exit 1
fi
grep -q "ERR001" .zero/native-test/check-maybe-void.err

if bin/zero check conformance/native/fail/raise-without-raises.0 2>.zero/native-test/raise-without-raises.err; then
  echo "expected raise-without-raises fixture to fail" >&2
  exit 1
fi
grep -q "ERR001" .zero/native-test/raise-without-raises.err

if bin/zero check conformance/native/fail/raise-undeclared-error.0 2>.zero/native-test/raise-undeclared-error.err; then
  echo "expected raise-undeclared-error fixture to fail" >&2
  exit 1
fi
grep -q "ERR002" .zero/native-test/raise-undeclared-error.err

if bin/zero check conformance/native/fail/unchecked-fallible-call.0 2>.zero/native-test/unchecked-fallible-call.err; then
  echo "expected unchecked-fallible-call fixture to fail" >&2
  exit 1
fi
grep -q "ERR003" .zero/native-test/unchecked-fallible-call.err

if bin/zero check conformance/native/fail/error-set-mismatch.0 2>.zero/native-test/error-set-mismatch.err; then
  echo "expected error-set-mismatch fixture to fail" >&2
  exit 1
fi
grep -q "ERR002" .zero/native-test/error-set-mismatch.err

if bin/zero check conformance/native/fail/const-mut-borrow.0 2>.zero/native-test/const-mut-borrow.err; then
  echo "expected const-mut-borrow fixture to fail" >&2
  exit 1
fi
grep -q "TYP009" .zero/native-test/const-mut-borrow.err

if bin/zero check conformance/native/fail/byte-buffer-use-after-move.0 2>.zero/native-test/byte-buffer-use-after-move.err; then
  echo "expected byte-buffer-use-after-move fixture to fail" >&2
  exit 1
fi
grep -q "OWN001" .zero/native-test/byte-buffer-use-after-move.err

if bin/zero check conformance/native/fail/test-expect-non-bool.0 2>.zero/native-test/test-expect-non-bool.err; then
  echo "expected test-expect-non-bool fixture to fail" >&2
  exit 1
fi
grep -q "TYP001" .zero/native-test/test-expect-non-bool.err

if bin/zero check conformance/native/fail/bad-c-export-raises.0 2>.zero/native-test/bad-c-export-raises.err; then
  echo "expected bad-c-export-raises fixture to fail" >&2
  exit 1
fi
grep -q "ABI001" .zero/native-test/bad-c-export-raises.err

if bin/zero check conformance/native/fail/unsupported-drop.0 2>.zero/native-test/unsupported-drop.err; then
  echo "expected unsupported-drop fixture to fail" >&2
  exit 1
fi
grep -q "OWN002" .zero/native-test/unsupported-drop.err

bin/zero targets > .zero/native-test/targets.json
grep -q '"schemaVersion": 1' .zero/native-test/targets.json
for target in \
  darwin-arm64 \
  darwin-x64 \
  linux-arm64 \
  linux-musl-arm64 \
  linux-musl-x64 \
  linux-x64 \
  win32-arm64.exe \
  win32-x64.exe
do
  grep -q "\"name\": \"$target\"" .zero/native-test/targets.json
done
node <<'NODE'
const fs = require("node:fs");

const targets = JSON.parse(fs.readFileSync(".zero/native-test/targets.json", "utf8")).targets;
const hosted = targets.find((target) => target.hosted === true);
const expected = ["memory", "stdio", "args", "env", "fs", "time", "rand", "net", "proc"];

if (!hosted) {
  console.error("expected one hosted target in target list");
  process.exit(1);
}

if (!hosted.aliases.includes("host")) {
  console.error("hosted target missing host alias");
  process.exit(1);
}

for (const capability of expected) {
  if (!hosted.capabilities.includes(capability)) {
    console.error(`hosted target missing capability: ${capability}`);
    process.exit(1);
  }
}

if (hosted.httpRuntime?.status !== "supported" ||
    hosted.httpRuntime?.provider !== "curl" ||
    hosted.httpRuntime?.tlsVerification !== true ||
    hosted.httpRuntime?.customCa?.env !== "ZERO_HTTP_TEST_CA_BUNDLE") {
  console.error("hosted target missing HTTP/TLS provider facts");
  process.exit(1);
}
NODE
grep -q '"objectFormat": "elf"' .zero/native-test/targets.json
grep -q '"name":"web","available":false' .zero/native-test/targets.json
grep -q "x86_64-windows-msvc" .zero/native-test/targets.json
grep -q '"sysrootStatus":"missing"' .zero/native-test/targets.json
bin/zero explain TAR002 > .zero/native-test/explain-tar002.txt
grep -q "Target capability unavailable" .zero/native-test/explain-tar002.txt
bin/zero explain --json TYP009 > .zero/native-test/explain-typ009.json
grep -q '"repair"' .zero/native-test/explain-typ009.json
bin/zero fix --plan --json conformance/native/fail/mem-copy-immutable-dst.0 > .zero/native-test/fix-plan.json
grep -q '"appliesEdits": false' .zero/native-test/fix-plan.json
bin/zero graph --json examples/point.0 > .zero/native-test/point-graph.json
grep -q '"shapes"' .zero/native-test/point-graph.json
bin/zero graph --json examples/systems-package > .zero/native-test/systems-package-graph.json
grep -q '"choices"' .zero/native-test/systems-package-graph.json
bin/zero size --json examples/point.0 > .zero/native-test/point-size.json
grep -q '"generatedCBytes"' .zero/native-test/point-size.json
bin/zero graph --json examples/memory-package > .zero/native-test/memory-package-graph-helpers.json
grep -q '"stdlibHelpers"' .zero/native-test/memory-package-graph-helpers.json
bin/zero size --json examples/memory-package > .zero/native-test/memory-package-size.json
grep -q '"stdlibHelpers"' .zero/native-test/memory-package-size.json
if bin/zero build --json --emit c --target linux-musl-x64 examples/hello.0 --out .zero/native-test/removed-c-backend.c > .zero/native-test/removed-c-backend.json; then
  echo "expected removed C backend to fail" >&2
  exit 1
fi
node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(process.argv[1],"utf8")); if (report.diagnostics?.[0]?.code!=="BLD003") process.exit(1);' .zero/native-test/removed-c-backend.json
if bin/zero build --json --legacy-backend --target linux-musl-x64 examples/hello.0 --out .zero/native-test/removed-legacy-backend > .zero/native-test/removed-legacy-backend.json; then
  echo "expected removed legacy backend flag to fail" >&2
  exit 1
fi
node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(process.argv[1],"utf8")); if (report.diagnostics?.[0]?.code!=="BLD003") process.exit(1);' .zero/native-test/removed-legacy-backend.json
rm -f .zero/native-test/direct-obj-add.o .zero/native-test/direct-obj-add.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-obj-add.0 --out .zero/native-test/direct-obj-add.o > .zero/native-test/direct-obj-add.json
node <<'NODE'
const fs = require("fs");
const b = fs.readFileSync(".zero/native-test/direct-obj-add.o");
function cstr(buf, off) {
  let end = off;
  while (end < buf.length && buf[end] !== 0) end++;
  return buf.toString("utf8", off, end);
}
if (b[0] !== 0x7f || b[1] !== 0x45 || b[2] !== 0x4c || b[3] !== 0x46) process.exit(1);
if (b[4] !== 2 || b[5] !== 1) process.exit(1);
if (b.readUInt16LE(16) !== 1 || b.readUInt16LE(18) !== 62) process.exit(1);
const shoff = Number(b.readBigUInt64LE(40));
const shentsize = b.readUInt16LE(58);
const shnum = b.readUInt16LE(60);
const shstrndx = b.readUInt16LE(62);
const shstr = {
  off: Number(b.readBigUInt64LE(shoff + shstrndx * shentsize + 24)),
  size: Number(b.readBigUInt64LE(shoff + shstrndx * shentsize + 32)),
};
const sections = new Map();
for (let i = 0; i < shnum; i++) {
  const off = shoff + i * shentsize;
  const name = cstr(b, shstr.off + b.readUInt32LE(off));
  sections.set(name, {
    index: i,
    type: b.readUInt32LE(off + 4),
    off: Number(b.readBigUInt64LE(off + 24)),
    size: Number(b.readBigUInt64LE(off + 32)),
    link: b.readUInt32LE(off + 40),
    info: b.readUInt32LE(off + 44),
    entsize: Number(b.readBigUInt64LE(off + 56)),
  });
}
if (!sections.has(".text") || !sections.has(".symtab") || !sections.has(".strtab") || !sections.has(".shstrtab")) process.exit(1);
const text = sections.get(".text");
if (text.size === 0) process.exit(1);
const symtab = sections.get(".symtab");
const strtabSection = [...sections.values()].find((section) => section.index === symtab.link);
let sawMain = false;
for (let off = symtab.off; off < symtab.off + symtab.size; off += symtab.entsize) {
  const nameOff = b.readUInt32LE(off);
  const name = cstr(b, strtabSection.off + nameOff);
  const info = b[off + 4];
  const shndx = b.readUInt16LE(off + 6);
  const size = Number(b.readBigUInt64LE(off + 16));
  if (name === "main" && info === 0x12 && shndx === text.index && size > 0) sawMain = true;
}
if (!sawMain) process.exit(1);
NODE
test ! -f .zero/native-test/direct-obj-add.o.c
grep -q '"emit": "obj"' .zero/native-test/direct-obj-add.json
grep -q '"compiler": "zero-elf64"' .zero/native-test/direct-obj-add.json
grep -q '"driverKind":"zero-elf64"' .zero/native-test/direct-obj-add.json
grep -q '"generatedCBytes": 0' .zero/native-test/direct-obj-add.json
grep -q '"loweredIrBytes": ' .zero/native-test/direct-obj-add.json
grep -q '"path":"direct-elf64-object"' .zero/native-test/direct-obj-add.json
rm -f .zero/native-test/direct-i64-return.o .zero/native-test/direct-i64-return.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-i64-return.0 --out .zero/native-test/direct-i64-return.o > .zero/native-test/direct-i64-return-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-i64-return.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62 || !b.includes(Buffer.from([0x48,0xb8,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x7f])) || !b.includes(Buffer.from([0x48,0x01,0xc8]))) process.exit(1);'
test ! -f .zero/native-test/direct-i64-return.o.c
grep -q '"path":"direct-elf64-object"' .zero/native-test/direct-i64-return-obj.json
rm -f .zero/native-test/direct-if-return.o .zero/native-test/direct-if-return.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-if-return.0 --out .zero/native-test/direct-if-return.o > .zero/native-test/direct-if-return-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-if-return.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-if-return.o.c
grep -q '"path":"direct-elf64-object"' .zero/native-test/direct-if-return-obj.json
rm -f .zero/native-test/direct-call-add.o .zero/native-test/direct-call-add.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-call-add.0 --out .zero/native-test/direct-call-add.o > .zero/native-test/direct-call-add-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-call-add.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62 || !b.includes(Buffer.concat([Buffer.from("add"),Buffer.from([0])])) || !b.includes(Buffer.concat([Buffer.from("main"),Buffer.from([0])]))) process.exit(1);'
test ! -f .zero/native-test/direct-call-add.o.c
grep -q '"path":"direct-elf64-object"' .zero/native-test/direct-call-add-obj.json
rm -f .zero/native-test/direct-call-add-linux-gnu.o .zero/native-test/direct-call-add-linux-gnu.o.c
bin/zero build --json --emit obj --target linux-x64 examples/direct-call-add.0 --out .zero/native-test/direct-call-add-linux-gnu.o > .zero/native-test/direct-call-add-linux-gnu.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-call-add-linux-gnu.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-call-add-linux-gnu.o.c
grep -q '"target": "linux-x64"' .zero/native-test/direct-call-add-linux-gnu.json
grep -q '"selectedEmitter":"zero-elf64"' .zero/native-test/direct-call-add-linux-gnu.json
rm -f .zero/native-test/direct-call-add-darwin.o .zero/native-test/direct-call-add-darwin.o.c
bin/zero build --json --emit obj --target darwin-arm64 examples/direct-call-add.0 --out .zero/native-test/direct-call-add-darwin.o > .zero/native-test/direct-call-add-darwin.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-call-add-darwin.o"); const section=32+72; const reloff=b.readUInt32LE(section+56); const nreloc=b.readUInt32LE(section+60); const info=reloff ? b.readUInt32LE(reloff+4) : 0; if (b.readUInt32LE(0)!==0xfeedfacf || b.readUInt32LE(4)!==0x0100000c || b.readUInt32LE(12)!==1 || !b.includes(Buffer.concat([Buffer.from("_main"),Buffer.from([0])])) || !b.includes(Buffer.concat([Buffer.from("_add"),Buffer.from([0])])) || !b.includes(Buffer.from([0x00,0x01,0x09,0x0b])) || reloff===0 || nreloc<1 || ((info>>>28)&15)!==2) process.exit(1);'
test ! -f .zero/native-test/direct-call-add-darwin.o.c
grep -q '"path":"direct-macho64-object"' .zero/native-test/direct-call-add-darwin.json
grep -q '"selectedEmitter":"zero-macho64"' .zero/native-test/direct-call-add-darwin.json
rm -f .zero/native-test/direct-byte-view-reloc-darwin.o .zero/native-test/direct-byte-view-reloc-darwin.o.c
bin/zero build --json --emit obj --target darwin-arm64 examples/direct-byte-view-reloc.0 --out .zero/native-test/direct-byte-view-reloc-darwin.o > .zero/native-test/direct-byte-view-reloc-darwin.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-byte-view-reloc-darwin.o"); const section=32+72; const reloff=b.readUInt32LE(section+56); const nreloc=b.readUInt32LE(section+60); const types=[]; for (let i=0;i<nreloc;i++) types.push((b.readUInt32LE(reloff+i*8+4)>>>28)&15); if (b.readUInt32LE(0)!==0xfeedfacf || b.readUInt32LE(4)!==0x0100000c || b.readUInt32LE(12)!==1 || !b.includes(Buffer.from("token")) || reloff===0 || !types.includes(3) || !types.includes(4) || types.includes(0)) process.exit(1);'
test ! -f .zero/native-test/direct-byte-view-reloc-darwin.o.c
grep -q '"dataSections":true' .zero/native-test/direct-byte-view-reloc-darwin.json
grep -q '"readonlyDataBytes":6' .zero/native-test/direct-byte-view-reloc-darwin.json
if command -v otool >/dev/null 2>&1; then
  otool -hvl .zero/native-test/direct-byte-view-reloc-darwin.o > .zero/native-test/direct-byte-view-reloc-darwin.otool
  grep -q 'nsects 2' .zero/native-test/direct-byte-view-reloc-darwin.otool
  grep -q 'sectname __const' .zero/native-test/direct-byte-view-reloc-darwin.otool
fi
if command -v file >/dev/null 2>&1; then
  file .zero/native-test/direct-byte-view-reloc-darwin.o > .zero/native-test/direct-byte-view-reloc-darwin.file
  grep -Eq 'Mach-O 64-bit.*arm64|Mach-O 64-bit.*ARM64' .zero/native-test/direct-byte-view-reloc-darwin.file
fi
rm -f .zero/native-test/direct-hello-darwin.o .zero/native-test/direct-hello-darwin.o.c
bin/zero build --json --emit obj --target darwin-arm64 examples/hello.0 --out .zero/native-test/direct-hello-darwin.o > .zero/native-test/direct-hello-darwin.json
node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/direct-hello-darwin.json","utf8")); const b=fs.readFileSync(".zero/native-test/direct-hello-darwin.o"); const section=32+72; const reloff=b.readUInt32LE(section+56); const nreloc=b.readUInt32LE(section+60); let sawBranch=false; for (let i=0;i<nreloc;i++){ if (((b.readUInt32LE(reloff+i*8+4)>>>28)&15)===2) sawBranch=true; } if (report.objectBackend.objectEmission.symbolCount!==3 || report.objectBackend.directFacts.runtimeHelperCount!==1 || b.readUInt32LE(0)!==0xfeedfacf || b.readUInt32LE(4)!==0x0100000c || b.readUInt32LE(12)!==1 || reloff===0 || nreloc<2 || !sawBranch || !b.includes(Buffer.from("hello from zero")) || !b.includes(Buffer.from("_zero_world_write"))) process.exit(1);'
test ! -f .zero/native-test/direct-hello-darwin.o.c
grep -q '"path":"direct-macho64-object"' .zero/native-test/direct-hello-darwin.json
grep -q '"generatedCBytes": 0' .zero/native-test/direct-hello-darwin.json
rm -f .zero/native-test/direct-std-args-darwin.o .zero/native-test/direct-std-args-darwin.o.c .zero/native-test/direct-std-args-darwin-linked .zero/native-test/direct-std-args-darwin-runtime.c .zero/native-test/direct-std-args-darwin-link.0 .zero/native-test/direct-std-args-darwin-link.o .zero/native-test/direct-std-args-darwin-link.json
bin/zero build --json --emit obj --target darwin-arm64 conformance/native/pass/std-args.0 --out .zero/native-test/direct-std-args-darwin.o > .zero/native-test/direct-std-args-darwin.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-std-args-darwin.o"); const save=Buffer.from([0xf4,0x57,0xbf,0xa9]); const seed=Buffer.from([0xf4,0x03,0x00,0xaa,0xf5,0x03,0x01,0xaa]); const restore=Buffer.from([0xf4,0x57,0xc1,0xa8]); if (b.readUInt32LE(0)!==0xfeedfacf || b.readUInt32LE(4)!==0x0100000c || !b.includes(save) || !b.includes(seed) || !b.includes(restore)) process.exit(1);'
test ! -f .zero/native-test/direct-std-args-darwin.o.c
grep -q '"path":"direct-macho64-object"' .zero/native-test/direct-std-args-darwin.json
if [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ] && command -v cc >/dev/null 2>&1; then
  cat > .zero/native-test/direct-std-args-darwin-link.0 <<'SOURCE'
pub fn main Void world World !
  let first std.args.get 1
  if first.has
    check world.out.write first.value
SOURCE
  cat > .zero/native-test/direct-std-args-darwin-runtime.c <<'SOURCE'
#include <unistd.h>
int zero_world_write(int fd, const char *buf, unsigned len) {
  ssize_t written = write(fd, buf, len);
  return written < 0 || (unsigned long long)written != len;
}
SOURCE
  bin/zero build --json --emit obj --target darwin-arm64 .zero/native-test/direct-std-args-darwin-link.0 --out .zero/native-test/direct-std-args-darwin-link.o > .zero/native-test/direct-std-args-darwin-link.json
  cc .zero/native-test/direct-std-args-darwin-link.o .zero/native-test/direct-std-args-darwin-runtime.c -o .zero/native-test/direct-std-args-darwin-linked
  direct_std_args_darwin_output="$(.zero/native-test/direct-std-args-darwin-linked agent-arg extra)"
  test "$direct_std_args_darwin_output" = "agent-arg"
  rm -f .zero/native-test/std-http-response-helpers .zero/native-test/std-http-response-helpers.json .zero/native-test/std-http-response-helpers.zero.o .zero/native-test/std-http-response-helpers.zero-runtime.o
  bin/zero build --json --emit exe --target darwin-arm64 conformance/native/pass/std-http-response-helpers.0 --out .zero/native-test/std-http-response-helpers > .zero/native-test/std-http-response-helpers.json
  set +e
  .zero/native-test/std-http-response-helpers
  std_http_response_helpers_status=$?
  set -e
  test "$std_http_response_helpers_status" = "29"
  test ! -f .zero/native-test/std-http-response-helpers.zero.o
  test ! -f .zero/native-test/std-http-response-helpers.zero-runtime.o
  node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/std-http-response-helpers.json","utf8")); if (report.generatedCBytes!==0 || report.objectBackend.linking.targetLibraries!=="zero-runtime" || report.objectBackend.linking.externalToolchain!=="cc" || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_runtime.o") || report.objectBackend.directFacts.runtimeHelperCount!==1) process.exit(1);'
  rm -f .zero/native-test/std-http-fetch .zero/native-test/std-http-fetch.json .zero/native-test/std-http-fetch.zero.o .zero/native-test/std-http-fetch.zero-runtime.o .zero/native-test/std-http-fetch.zero-http-curl.o
  bin/zero build --json --emit exe --target darwin-arm64 conformance/native/pass/std-http-fetch.0 --out .zero/native-test/std-http-fetch > .zero/native-test/std-http-fetch.json
  set +e
  .zero/native-test/std-http-fetch
  std_http_fetch_status=$?
  set -e
  test "$std_http_fetch_status" = "23"
  test ! -f .zero/native-test/std-http-fetch.zero.o
  test ! -f .zero/native-test/std-http-fetch.zero-runtime.o
  test ! -f .zero/native-test/std-http-fetch.zero-http-curl.o
  node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/std-http-fetch.json","utf8")); if (report.generatedCBytes!==0 || report.objectBackend.linking.targetLibraries!=="zero-runtime,curl" || report.objectBackend.linking.externalToolchain!=="cc" || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_runtime.o") || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_http_curl.o") || !report.objectBackend.linkerPlan.systemLibraries.includes("curl") || report.objectBackend.directFacts.runtimeHelperCount!==2 || !(report.objectBackend.directFacts.runtime.readonlyDataBytes > 0)) process.exit(1);'
  scripts/http-runtime-smoke.mts
fi
rm -f .zero/native-test/direct-call-add-win.obj .zero/native-test/direct-call-add-win.obj.c
bin/zero build --json --emit obj --target win32-x64.exe examples/direct-call-add.0 --out .zero/native-test/direct-call-add-win.obj > .zero/native-test/direct-call-add-win.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-call-add-win.obj"); const reloc=b.readUInt32LE(20+24); const nreloc=b.readUInt16LE(20+32); if (b.readUInt16LE(0)!==0x8664 || b.readUInt16LE(2)!==1 || b.readUInt32LE(8)===0 || reloc===0 || nreloc<1 || b.readUInt16LE(reloc+8)!==4 || !b.includes(Buffer.from([0xe8])) || !b.includes(Buffer.concat([Buffer.from("main"),Buffer.from([0])])) || !b.includes(Buffer.concat([Buffer.from("add"),Buffer.from([0])]))) process.exit(1);'
test ! -f .zero/native-test/direct-call-add-win.obj.c
grep -q '"path":"direct-coff-x64-object"' .zero/native-test/direct-call-add-win.json
grep -q '"selectedEmitter":"zero-coff-x64"' .zero/native-test/direct-call-add-win.json
rm -f .zero/native-test/direct-byte-view-reloc-win.obj .zero/native-test/direct-byte-view-reloc-win.obj.c
bin/zero build --json --emit obj --target win32-x64.exe examples/direct-byte-view-reloc.0 --out .zero/native-test/direct-byte-view-reloc-win.obj > .zero/native-test/direct-byte-view-reloc-win.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-byte-view-reloc-win.obj"); const reloc=b.readUInt32LE(20+24); const nreloc=b.readUInt16LE(20+32); let sawAddr64=false; for (let i=0;i<nreloc;i++){ if (b.readUInt16LE(reloc+i*10+8)===1) sawAddr64=true; } if (b.readUInt16LE(0)!==0x8664 || b.readUInt16LE(2)!==2 || reloc===0 || nreloc<1 || !sawAddr64 || !b.includes(Buffer.from(".rdata\0")) || !b.includes(Buffer.from("token"))) process.exit(1);'
test ! -f .zero/native-test/direct-byte-view-reloc-win.obj.c
grep -q '"dataSections":true' .zero/native-test/direct-byte-view-reloc-win.json
grep -q '"readonlyDataBytes":6' .zero/native-test/direct-byte-view-reloc-win.json
rm -f .zero/native-test/direct-hello-win.obj .zero/native-test/direct-hello-win.obj.c
bin/zero build --json --emit obj --target win32-x64.exe examples/hello.0 --out .zero/native-test/direct-hello-win.obj > .zero/native-test/direct-hello-win.json
node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/direct-hello-win.json","utf8")); const b=fs.readFileSync(".zero/native-test/direct-hello-win.obj"); const reloc=b.readUInt32LE(20+24); const nreloc=b.readUInt16LE(20+32); let sawRel32=false; for (let i=0;i<nreloc;i++){ if (b.readUInt16LE(reloc+i*10+8)===4) sawRel32=true; } if (report.objectBackend.objectEmission.symbolCount!==4 || report.objectBackend.directFacts.runtimeHelperCount!==1 || b.readUInt16LE(0)!==0x8664 || b.readUInt16LE(2)!==2 || reloc===0 || nreloc<2 || !sawRel32 || !b.includes(Buffer.from("hello from zero")) || !b.includes(Buffer.from("zero_world_write"))) process.exit(1);'
test ! -f .zero/native-test/direct-hello-win.obj.c
grep -q '"path":"direct-coff-x64-object"' .zero/native-test/direct-hello-win.json
grep -q '"generatedCBytes": 0' .zero/native-test/direct-hello-win.json
rm -f .zero/native-test/direct-array-fill.o .zero/native-test/direct-array-fill.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-array-fill.0 --out .zero/native-test/direct-array-fill.o > .zero/native-test/direct-array-fill-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-array-fill.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-array-fill.o.c
grep -q '"maxFrameBytes":' .zero/native-test/direct-array-fill-obj.json
rm -f .zero/native-test/direct-package-arrays.o .zero/native-test/direct-package-arrays.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-package-arrays --out .zero/native-test/direct-package-arrays.o > .zero/native-test/direct-package-arrays-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-package-arrays.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-package-arrays.o.c
grep -q '"moduleCount":2' .zero/native-test/direct-package-arrays-obj.json
rm -f .zero/native-test/direct-token-shape.o .zero/native-test/direct-token-shape.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-token-shape.0 --out .zero/native-test/direct-token-shape.o > .zero/native-test/direct-token-shape-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-token-shape.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-token-shape.o.c
grep -q '"path":"direct-elf64-object"' .zero/native-test/direct-token-shape-obj.json
rm -f .zero/native-test/direct-span-read.o .zero/native-test/direct-span-read.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-span-read.0 --out .zero/native-test/direct-span-read.o > .zero/native-test/direct-span-read-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-span-read.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-span-read.o.c
grep -q '"dataSections":true' .zero/native-test/direct-span-read-obj.json
grep -q '"readonlyDataBytes":6' .zero/native-test/direct-span-read-obj.json
rm -f .zero/native-test/direct-byte-view-reloc.o .zero/native-test/direct-byte-view-reloc.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-byte-view-reloc.0 --out .zero/native-test/direct-byte-view-reloc.o > .zero/native-test/direct-byte-view-reloc-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-byte-view-reloc.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62 || !b.includes(Buffer.concat([Buffer.from(".rodata"),Buffer.from([0])])) || !b.includes(Buffer.concat([Buffer.from(".rela.text"),Buffer.from([0])]))) process.exit(1);'
test ! -f .zero/native-test/direct-byte-view-reloc.o.c
grep -q '"readonlyDataBytes":6' .zero/native-test/direct-byte-view-reloc-obj.json
rm -f .zero/native-test/direct-rescue-basic.o .zero/native-test/direct-rescue-basic.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-rescue-basic.0 --out .zero/native-test/direct-rescue-basic.o > .zero/native-test/direct-rescue-basic-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-rescue-basic.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-rescue-basic.o.c
grep -q '"path":"direct-elf64-object"' .zero/native-test/direct-rescue-basic-obj.json
rm -f .zero/native-test/direct-exe-return .zero/native-test/direct-exe-return.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-exe-return.0 --out .zero/native-test/direct-exe-return > .zero/native-test/direct-exe-return.json
node <<'NODE'
const fs = require("fs");
const b = fs.readFileSync(".zero/native-test/direct-exe-return");
if (b[0] !== 0x7f || b[1] !== 0x45 || b[2] !== 0x4c || b[3] !== 0x46) process.exit(1);
if (b[4] !== 2 || b[5] !== 1) process.exit(1);
if (b.readUInt16LE(16) !== 2 || b.readUInt16LE(18) !== 62) process.exit(1);
if (Number(b.readBigUInt64LE(24)) === 0) process.exit(1);
if (Number(b.readBigUInt64LE(32)) !== 64) process.exit(1);
if (b.readUInt16LE(54) !== 56 || b.readUInt16LE(56) !== 1) process.exit(1);
const phoff = Number(b.readBigUInt64LE(32));
if (b.readUInt32LE(phoff) !== 1 || b.readUInt32LE(phoff + 4) !== 5) process.exit(1);
if (Number(b.readBigUInt64LE(phoff + 32)) === 0) process.exit(1);
if (Number(b.readBigUInt64LE(phoff + 32)) !== Number(b.readBigUInt64LE(phoff + 40))) process.exit(1);
NODE
test ! -f .zero/native-test/direct-exe-return.c
grep -q '"emit": "exe"' .zero/native-test/direct-exe-return.json
grep -q '"compiler": "zero-elf64"' .zero/native-test/direct-exe-return.json
grep -q '"driverKind":"zero-elf64"' .zero/native-test/direct-exe-return.json
grep -q '"generatedCBytes": 0' .zero/native-test/direct-exe-return.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-exe-return.json
node -e 'const r=require("fs").readFileSync(".zero/native-test/direct-exe-return.json","utf8"); const j=JSON.parse(r); if (j.artifactBytes >= 512 || j.objectBackend.objectEmission.dataSections !== false || j.objectBackend.linking.externalToolchain !== "none") process.exit(1);'
rm -f .zero/native-test/direct-aarch64-exe-return .zero/native-test/direct-aarch64-exe-return.c
bin/zero build --json --emit exe --backend zero-elf-aarch64 --target linux-musl-arm64 examples/direct-exe-return.0 --out .zero/native-test/direct-aarch64-exe-return > .zero/native-test/direct-aarch64-exe-return.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-aarch64-exe-return"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==183 || !b.includes(Buffer.from([0x40,0x05,0x80,0x52,0xc0,0x03,0x5f,0xd6])) || !b.includes(Buffer.from([0xa8,0x0b,0x80,0xd2,0x01,0x00,0x00,0xd4]))) process.exit(1);'
test ! -f .zero/native-test/direct-aarch64-exe-return.c
grep -q '"compiler": "zero-elf-aarch64"' .zero/native-test/direct-aarch64-exe-return.json
grep -q '"path":"direct-elf-aarch64-exe"' .zero/native-test/direct-aarch64-exe-return.json
node -e 'const r=require("fs").readFileSync(".zero/native-test/direct-aarch64-exe-return.json","utf8"); const j=JSON.parse(r); if (j.artifactBytes >= 512 || j.objectBackend.targetFacts.status !== "native-exe" || j.objectBackend.linking.externalToolchain !== "none") process.exit(1);'
rm -f .zero/native-test/direct-while-sum .zero/native-test/direct-while-sum.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-while-sum.0 --out .zero/native-test/direct-while-sum > .zero/native-test/direct-while-sum.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-while-sum"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-while-sum.c
grep -q '"generatedCBytes": 0' .zero/native-test/direct-while-sum.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-while-sum.json
rm -f .zero/native-test/direct-call-loop .zero/native-test/direct-call-loop.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-call-loop.0 --out .zero/native-test/direct-call-loop > .zero/native-test/direct-call-loop.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-call-loop"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-call-loop.c
grep -q '"generatedCBytes": 0' .zero/native-test/direct-call-loop.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-call-loop.json
rm -f .zero/native-test/direct-array-fill .zero/native-test/direct-array-fill.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-array-fill.0 --out .zero/native-test/direct-array-fill > .zero/native-test/direct-array-fill.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-array-fill"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-array-fill.c
grep -q '"stackBytes":' .zero/native-test/direct-array-fill.json
rm -f .zero/native-test/direct-package-arrays .zero/native-test/direct-package-arrays.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-package-arrays --out .zero/native-test/direct-package-arrays > .zero/native-test/direct-package-arrays.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-package-arrays"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-package-arrays.c
grep -q '"moduleCount":2' .zero/native-test/direct-package-arrays.json
rm -f .zero/native-test/direct-token-shape .zero/native-test/direct-token-shape.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-token-shape.0 --out .zero/native-test/direct-token-shape > .zero/native-test/direct-token-shape.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-token-shape"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-token-shape.c
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-token-shape.json
rm -f .zero/native-test/direct-string-len .zero/native-test/direct-string-len.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-string-len.0 --out .zero/native-test/direct-string-len > .zero/native-test/direct-string-len.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-string-len"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-string-len.c
grep -q '"readonlyDataBytes":6' .zero/native-test/direct-string-len.json
rm -f .zero/native-test/direct-string-literal .zero/native-test/direct-string-literal.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-string-literal.0 --out .zero/native-test/direct-string-literal > .zero/native-test/direct-string-literal.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-string-literal"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-string-literal.c
grep -q '"readonlyDataBytes":6' .zero/native-test/direct-string-literal.json
rm -f .zero/native-test/direct-span-read .zero/native-test/direct-span-read.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-span-read.0 --out .zero/native-test/direct-span-read > .zero/native-test/direct-span-read.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-span-read"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-span-read.c
grep -q '"readonlyDataBytes":6' .zero/native-test/direct-span-read.json
rm -f .zero/native-test/direct-byte-view-reloc .zero/native-test/direct-byte-view-reloc.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-byte-view-reloc.0 --out .zero/native-test/direct-byte-view-reloc > .zero/native-test/direct-byte-view-reloc.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-byte-view-reloc"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-byte-view-reloc.c
grep -q '"readonlyDataBytes":6' .zero/native-test/direct-byte-view-reloc.json
node -e 'const r=require("fs").readFileSync(".zero/native-test/direct-byte-view-reloc.json","utf8"); const j=JSON.parse(r); if (j.artifactBytes >= 1024 || j.objectBackend.objectEmission.dataSections !== true || j.objectBackend.linking.externalToolchain !== "none") process.exit(1);'
rm -f .zero/native-test/direct-raises-basic .zero/native-test/direct-raises-basic.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-raises-basic.0 --out .zero/native-test/direct-raises-basic > .zero/native-test/direct-raises-basic.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-raises-basic"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-raises-basic.c
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-raises-basic.json
rm -f .zero/native-test/direct-rescue-basic .zero/native-test/direct-rescue-basic.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-rescue-basic.0 --out .zero/native-test/direct-rescue-basic > .zero/native-test/direct-rescue-basic.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-rescue-basic"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-rescue-basic.c
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-rescue-basic.json
rm -f .zero/native-test/direct-unhandled-error-exit .zero/native-test/direct-unhandled-error-exit.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-unhandled-error-exit.0 --out .zero/native-test/direct-unhandled-error-exit > .zero/native-test/direct-unhandled-error-exit.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-unhandled-error-exit"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-unhandled-error-exit.c
grep -q '"generatedCBytes": 0' .zero/native-test/direct-unhandled-error-exit.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-unhandled-error-exit.json
rm -f .zero/native-test/direct-std-fs-breadth .zero/native-test/direct-std-fs-breadth.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 conformance/native/pass/std-fs-breadth.0 --out .zero/native-test/direct-std-fs-breadth > .zero/native-test/direct-std-fs-breadth.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-std-fs-breadth"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-std-fs-breadth.c
grep -q '"generatedCBytes": 0' .zero/native-test/direct-std-fs-breadth.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-std-fs-breadth.json
rm -f .zero/native-test/direct-std-fs-fallible-resources .zero/native-test/direct-std-fs-fallible-resources.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 conformance/native/pass/std-fs-fallible-resources.0 --out .zero/native-test/direct-std-fs-fallible-resources > .zero/native-test/direct-std-fs-fallible-resources.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-std-fs-fallible-resources"); const packed=(code)=>{const x=Buffer.alloc(10); x[0]=0x48; x[1]=0xb8; x.writeBigUInt64LE(BigInt(code)<<32n,2); return x}; if (!b.includes(packed(2)) || !b.includes(packed(4))) process.exit(1);'
test ! -f .zero/native-test/direct-std-fs-fallible-resources.c
grep -q '"generatedCBytes": 0' .zero/native-test/direct-std-fs-fallible-resources.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-std-fs-fallible-resources.json
rm -f .zero/native-test/direct-std-fs-fallible .zero/native-test/direct-std-fs-fallible.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 conformance/native/pass/std-fs-fallible.0 --out .zero/native-test/direct-std-fs-fallible > .zero/native-test/direct-std-fs-fallible.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-std-fs-fallible"); const packed=(code)=>{const x=Buffer.alloc(10); x[0]=0x48; x[1]=0xb8; x.writeBigUInt64LE(BigInt(code)<<32n,2); return x}; if (!b.includes(packed(2)) || !b.includes(packed(3)) || !b.includes(packed(4))) process.exit(1);'
test ! -f .zero/native-test/direct-std-fs-fallible.c
grep -q '"generatedCBytes": 0' .zero/native-test/direct-std-fs-fallible.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-std-fs-fallible.json
rm -f .zero/native-test/direct-std-io .zero/native-test/direct-std-io.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 conformance/native/pass/std-io-direct.0 --out .zero/native-test/direct-std-io > .zero/native-test/direct-std-io.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-std-io"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-std-io.c
grep -q '"generatedCBytes": 0' .zero/native-test/direct-std-io.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-std-io.json
if [ "$(uname -s)" = "Linux" ] && [ "$(uname -m)" = "x86_64" ]; then
  set +e
  .zero/native-test/direct-exe-return
  direct_exe_rc=$?
  set -e
  test "$direct_exe_rc" -eq 42
  set +e
  .zero/native-test/direct-while-sum
  direct_while_rc=$?
  set -e
  test "$direct_while_rc" -eq 10
  set +e
  .zero/native-test/direct-call-loop
  direct_call_loop_rc=$?
  set -e
  test "$direct_call_loop_rc" -eq 10
  set +e
  .zero/native-test/direct-array-fill
  direct_array_fill_rc=$?
  set -e
  test "$direct_array_fill_rc" -eq 20
  set +e
  .zero/native-test/direct-package-arrays
  direct_package_arrays_rc=$?
  set -e
  test "$direct_package_arrays_rc" -eq 13
  set +e
  .zero/native-test/direct-token-shape
  direct_token_shape_rc=$?
  set -e
  test "$direct_token_shape_rc" -eq 65
  set +e
  .zero/native-test/direct-string-len
  direct_string_len_rc=$?
  set -e
  test "$direct_string_len_rc" -eq 5
  set +e
  .zero/native-test/direct-string-literal
  direct_string_literal_rc=$?
  set -e
  test "$direct_string_literal_rc" -eq 116
  set +e
  .zero/native-test/direct-span-read
  direct_span_read_rc=$?
  set -e
  test "$direct_span_read_rc" -eq 107
  set +e
  .zero/native-test/direct-byte-view-reloc
  direct_byte_view_locals_rc=$?
  set -e
  test "$direct_byte_view_locals_rc" -eq 107
  set +e
  .zero/native-test/direct-raises-basic
  direct_raises_basic_rc=$?
  set -e
  test "$direct_raises_basic_rc" -eq 7
  set +e
  .zero/native-test/direct-rescue-basic
  direct_rescue_basic_rc=$?
  set -e
  test "$direct_rescue_basic_rc" -eq 9
  set +e
  .zero/native-test/direct-unhandled-error-exit
  direct_unhandled_error_rc=$?
  set -e
  test "$direct_unhandled_error_rc" -eq 1
  direct_std_fs_breadth_output="$(.zero/native-test/direct-std-fs-breadth)"
  test "$direct_std_fs_breadth_output" = "std fs breadth ok"
  direct_std_io_output="$(.zero/native-test/direct-std-io)"
  test "$direct_std_io_output" = "std io direct ok"
fi
if bin/zero build --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-obj-add.0 --out .zero/native-test/direct-exe-with-params >/dev/null 2>.zero/native-test/direct-exe-with-params.err; then
  echo "expected direct executable ABI gate to fail" >&2
  exit 1
fi
grep -q "must not take parameters" .zero/native-test/direct-exe-with-params.err
rm -f .zero/native-test/direct-exe-darwin .zero/native-test/direct-exe-darwin.c .zero/native-test/direct-exe-darwin.json .zero/native-test/direct-hello-darwin .zero/native-test/direct-hello-darwin.c
bin/zero build --json --emit exe --backend zero-macho64 --target darwin-arm64 examples/direct-exe-return.0 --out .zero/native-test/direct-exe-darwin > .zero/native-test/direct-exe-darwin.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-exe-darwin"); let sawUuid=false; for (let o=32,i=0,n=b.readUInt32LE(16); i<n; i++){ const cmd=b.readUInt32LE(o); const size=b.readUInt32LE(o+4); if (size<8 || o+size>b.length) process.exit(1); if (cmd===0x1b){ if (size!==24 || b.subarray(o+8,o+24).every((byte)=>byte===0)) process.exit(1); sawUuid=true; } o+=size; } if (b.readUInt32LE(0)!==0xfeedfacf || b.readUInt32LE(12)!==2 || !sawUuid || !b.includes(Buffer.from("/usr/lib/dyld")) || !b.includes(Buffer.from("zero-direct"))) process.exit(1);'
grep -q '"compiler": "zero-macho64"' .zero/native-test/direct-exe-darwin.json
grep -q '"path":"direct-macho64-exe"' .zero/native-test/direct-exe-darwin.json
grep -q '"generatedCBytes": 0' .zero/native-test/direct-exe-darwin.json
test ! -f .zero/native-test/direct-exe-darwin.c
if [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ]; then
  set +e
  .zero/native-test/direct-exe-darwin
  direct_macho_rc=$?
  set -e
  test "$direct_macho_rc" -eq 42
  direct_macho_hello_output="$(bin/zero build --emit exe --target darwin-arm64 examples/hello.0 --out .zero/native-test/direct-hello-darwin >/dev/null && .zero/native-test/direct-hello-darwin)"
  test "$direct_macho_hello_output" = "hello from zero"
  direct_macho_scratch_output="$(bin/zero build --emit exe --target darwin-arm64 conformance/native/pass/scratch-nested-index.0 --out .zero/native-test/direct-macho-scratch-nested-index >/dev/null && .zero/native-test/direct-macho-scratch-nested-index)"
  test "$direct_macho_scratch_output" = "scratch nested index ok"
fi
rm -f .zero/native-test/direct-arm64.o .zero/native-test/direct-arm64.o.c
bin/zero build --json --emit obj --target linux-arm64 examples/direct-exe-return.0 --out .zero/native-test/direct-arm64.o > .zero/native-test/direct-arm64.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-arm64.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==183 || !b.includes(Buffer.from([0x40,0x05,0x80,0x52,0xc0,0x03,0x5f,0xd6]))) process.exit(1);'
grep -q '"compiler": "zero-elf-aarch64"' .zero/native-test/direct-arm64.json
grep -q '"path":"direct-elf-aarch64-object"' .zero/native-test/direct-arm64.json
test ! -f .zero/native-test/direct-arm64.o.c
rm -f .zero/native-test/hello-linux-musl-arm64 .zero/native-test/hello-linux-musl-arm64.c
bin/zero build --emit exe --target linux-musl-arm64 examples/direct-exe-return.0 --out .zero/native-test/hello-linux-musl-arm64 >/dev/null
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/hello-linux-musl-arm64"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==183) process.exit(1);'
test ! -f .zero/native-test/hello-linux-musl-arm64.c
rm -f .zero/native-test/hello-windows.exe .zero/native-test/hello-windows.exe.c .zero/native-test/hello-windows.json
ZERO_CC=/usr/bin/false bin/zero build --json --emit exe --target win32-x64.exe examples/hello.0 --out .zero/native-test/hello-windows > .zero/native-test/hello-windows.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/hello-windows.exe"); const pe=b.readUInt32LE(0x3c); if (b[0]!==0x4d || b[1]!==0x5a || b.toString("ascii", pe, pe+4)!=="PE\0\0" || b.readUInt16LE(pe+4)!==0x8664 || !b.includes(Buffer.from("KERNEL32.dll")) || !b.includes(Buffer.from("WriteFile"))) process.exit(1);'
grep -q '"compiler": "zero-coff-x64"' .zero/native-test/hello-windows.json
grep -q '"path":"direct-coff-x64-exe"' .zero/native-test/hello-windows.json
grep -q '"generatedCBytes": 0' .zero/native-test/hello-windows.json
test ! -f .zero/native-test/hello-windows.exe.c
bin/zero check --target x86_64-windows-msvc examples/hello.0 >/dev/null
if command -v zig >/dev/null 2>&1; then
  bin/zero build --emit exe --target linux-musl-x64 examples/hello.0 --out .zero/native-test/hello-linux-musl >/dev/null
  test -f .zero/native-test/hello-linux-musl
fi
ZERO_CC=cc bin/zero build --emit exe --target linux-musl-x64 --profile dev examples/hello.0 --out .zero/native-test/hello-dev >/dev/null
bin/zero doctor --json > .zero/native-test/doctor.json
grep -q '"targetToolchains":' .zero/native-test/doctor.json
grep -q '"target": "darwin-arm64"' .zero/native-test/doctor.json
grep -q '"target": "linux-musl-x64"' .zero/native-test/doctor.json
grep -q '"target": "win32-x64.exe"' .zero/native-test/doctor.json
grep -q '"driverKind": "host-cc"' .zero/native-test/doctor.json
grep -q '"driverKind": "target-cc"' .zero/native-test/doctor.json
grep -q '"sysrootStatus":' .zero/native-test/doctor.json
bin/zero doctor > .zero/native-test/doctor.txt
grep -q "target toolchains:" .zero/native-test/doctor.txt

echo "native conformance ok"
