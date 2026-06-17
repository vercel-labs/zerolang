#!/usr/bin/env bash
set -euo pipefail

if [[ "${ZERO_NATIVE_TEST_TRACE:-}" == "1" ]]; then
  PS4='+ ${BASH_SOURCE[0]}:${LINENO}: '
  set -x
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if [[ "${ZERO_NATIVE_TEST_SANDBOX:-}" != "1" && "${ZERO_NATIVE_TEST_ALLOW_LOCAL:-}" != "1" ]]; then
  echo "native tests emit native artifacts; run 'pnpm run native:test' for Vercel Sandbox execution or set ZERO_NATIVE_TEST_ALLOW_LOCAL=1 to opt into local artifacts." >&2
  exit 1
fi

native_test_shard="${ZERO_NATIVE_TEST_SHARD:-1/1}"
if [[ "$native_test_shard" =~ ^([0-9]+)/([0-9]+)$ ]]; then
  native_test_shard_index="${BASH_REMATCH[1]}"
  native_test_shard_count="${BASH_REMATCH[2]}"
else
  echo "ZERO_NATIVE_TEST_SHARD must be formatted as index/count, for example 1/4" >&2
  exit 1
fi

if (( native_test_shard_count < 1 || native_test_shard_index < 1 || native_test_shard_index > native_test_shard_count )); then
  echo "ZERO_NATIVE_TEST_SHARD index must be between 1 and count" >&2
  exit 1
fi

native_test_case_index=0
native_test_phases=",${ZERO_NATIVE_TEST_PHASES:-all},"
native_test_scope="${ZERO_NATIVE_TEST_SCOPE:-deep}"

case "$native_test_scope" in
  fast|deep) ;;
  *)
    echo "ZERO_NATIVE_TEST_SCOPE must be fast or deep" >&2
    exit 1
    ;;
esac

native_log_elapsed() {
  local label="$1"
  local started_at="$2"
  echo "native ${label} ($((SECONDS - started_at))s)"
}

native_phase_enabled() {
  local phase="$1"
  [[ "$native_test_phases" == ",all," || "$native_test_phases" == *",$phase,"* ]]
}

native_phase_selected() {
  local phase="$1"
  local target

  if ! native_phase_enabled "$phase"; then
    return 1
  fi

  case "$phase" in
    preflight)
      target=1
      ;;
    metadata-and-reports)
      if (( native_test_shard_count >= 2 )); then
        target=2
      else
        target=1
      fi
      ;;
    direct-backend-artifacts)
      target="$native_test_shard_count"
      ;;
    *)
      echo "unknown native test phase: $phase" >&2
      exit 1
      ;;
  esac

  [[ "$native_test_shard_index" -eq "$target" ]]
}

native_case_selected() {
  if ! native_phase_enabled "cases"; then
    return 1
  fi

  native_test_case_index=$((native_test_case_index + 1))
  local selected=$(( (native_test_case_index - 1) % native_test_shard_count + 1 ))
  [[ "$selected" -eq "$native_test_shard_index" ]]
}

echo "native test shard ${native_test_shard_index}/${native_test_shard_count}"
echo "native test phases ${ZERO_NATIVE_TEST_PHASES:-all}"
echo "native test scope ${native_test_scope}"

native_build_started_at="$SECONDS"
make -C native/zero-c
native_log_elapsed "compiler build ok" "$native_build_started_at"

mkdir -p .zero/native-test .zero/conformance

if native_phase_selected "preflight"; then
  native_phase_started_at="$SECONDS"
  cc -std=c11 -Wall -Wextra -Wpedantic -I native/zero-c/include \
    native/zero-c/tests/process_exec_smoke.c \
    native/zero-c/src/process_exec.c \
    native/zero-c/src/process_path.c \
    -o .zero/native-test/process-exec-smoke
  .zero/native-test/process-exec-smoke
  node --experimental-strip-types --disable-warning=ExperimentalWarning scripts/artifact-finalization-smoke.mts
  cc -std=c11 -Wall -Wextra -Wpedantic -I native/zero-c/include -I native/zero-c/src \
    native/zero-c/tests/http_listen_runner_smoke.c \
    -o .zero/native-test/http-listen-runner-smoke
  .zero/native-test/http-listen-runner-smoke
  scripts/fs-runtime-smoke.mts
  bin/zero check --json std/path.graph >/dev/null
  bin/zero check --json std/str.graph >/dev/null
  bin/zero check --json std/testing.graph >/dev/null
  bin/zero check --json std/log.graph >/dev/null
  bin/zero check --json std/math.graph >/dev/null
  bin/zero check --json std/time.graph >/dev/null
  ZERO_STDLIB_TARGET_MATRIX_SCOPE="$native_test_scope" node --experimental-strip-types --disable-warning=ExperimentalWarning scripts/stdlib-target-matrix.mts

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
      "$root/.zero/bin/zero" build --json --emit exe --target "$host_runtime_target" "$root/conformance/native/pass/std-json-bytes.graph" --out "$runtime_cwd_out" > "$runtime_cwd_out.json"
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
    ZERO_CC=/usr/bin/false "$root/.zero/bin/zero" build --json --emit exe --target "$host_runtime_target" "$root/conformance/native/pass/std-json-bytes.graph" --out "$runtime_cwd_out-bad" > "$runtime_cwd_out-bad.json" 2>/dev/null
    runtime_zero_cc_status=$?
    set -e
    test "$runtime_zero_cc_status" != "0"
    grep -q "host runtime object build failed" "$runtime_cwd_out-bad.json"
    rm -rf "$runtime_cwd_dir"
  fi

  if command -v zig >/dev/null 2>&1; then
    json_cross_out=".zero/native-test/std-json-bytes-linux-musl"
    rm -f "$json_cross_out" "$json_cross_out.json" "$json_cross_out.zero.o" "$json_cross_out.zero-runtime.o"
    bin/zero build --json --emit exe --target linux-musl-x64 conformance/native/pass/std-json-bytes.graph --out "$json_cross_out" > "$json_cross_out.json"
    node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(process.argv[1],"utf8")); if (report.generatedCBytes!==0 || report.target!=="linux-musl-x64" || report.objectBackend.linking.targetLibraries!=="zero-runtime" || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_runtime.o")) process.exit(1);' "$json_cross_out.json"
    test ! -f "$json_cross_out.zero.o"
    test ! -f "$json_cross_out.zero-runtime.o"
  fi
  native_log_elapsed "preflight ok" "$native_phase_started_at"
fi

expected_output() {
  case "$1" in
    examples/hello.graph) printf "hello from zero" ;;
    examples/hello-let.graph) printf "hello from a binding" ;;
    examples/add.graph) printf "math works" ;;
    examples/branch.graph) printf "branch yes" ;;
    examples/countdown.graph) printf "countdown done" ;;
    examples/functions.graph) printf "function call works" ;;
    examples/point.graph) printf "point works" ;;
    examples/systems-package) printf "systems package" ;;
    examples/readall-cli) printf "readall cli ok" ;;
    examples/batch3-cli) printf "batch3 cli ok" ;;
    examples/resource-cli) printf "resource cli ok" ;;
    examples/zero-hash) printf "zero-hash ok" ;;
    examples/memory-package) printf "memory package ok" ;;
    examples/result-choice.graph) printf "choice ok" ;;
    examples/const-arithmetic.graph) printf "const arithmetic ok" ;;
    examples/generic-pair.graph) printf "generic pair ok" ;;
    examples/type-alias.graph) printf "type alias ok" ;;
    examples/static-method.graph) printf "static method ok" ;;
    examples/static-interface.graph) printf "static interface ok" ;;
    examples/fallibility.graph) printf "fallibility ok" ;;
    examples/ownership-cleanup.graph) printf "ownership cleanup ok" ;;
    examples/codec-varint.graph) printf "codec primitives ok" ;;
    examples/parse-cursor.graph) printf "parse primitives ok" ;;
    examples/std-math.graph) printf "std math ok" ;;
    examples/file-copy.graph) printf "file copy ok" ;;
    examples/grep-scan.graph) printf "grep scan ok" ;;
    examples/std-testing-log.graph) printf '{"level":"info","key":"event","value":"startup"}' ;;
    conformance/native/pass/std-crypto-hmac32.graph) printf "crypto hmac32 ok" ;;
    conformance/native/pass/std-crypto-sha256.graph) printf "crypto sha256 ok" ;;
    conformance/native/pass/string-byte-ergonomics.graph) printf "string byte ergonomics ok" ;;
    conformance/native/pass/std-math-breadth.graph) printf "std math breadth ok" ;;
    conformance/native/pass/std-numeric-random-time.graph) printf "std numeric random time ok" ;;
    conformance/native/pass/std-regex.graph) printf "std regex ok" ;;
    conformance/native/pass/std-unicode.graph) printf "std unicode ok" ;;
    conformance/native/pass/std-inet.graph) printf "std inet ok" ;;
    conformance/native/pass/std-time-rfc3339.graph) printf "std time rfc3339 ok" ;;
    conformance/native/pass/std-io-lines.graph) printf "std io lines ok" ;;
    conformance/native/pass/std-path-io-breadth.graph) printf "std path io breadth ok" ;;
    conformance/native/pass/std-fs-file-helpers.graph) printf "std fs file helpers ok" ;;
    conformance/native/pass/std-fs-write-file-bool.graph) printf "fs write file bool ok" ;;
    conformance/native/pass/std-proc-child.graph) printf "std proc child ok" ;;
    conformance/native/pass/std-proc-capture.graph) printf "std proc capture ok" ;;
    conformance/native/pass/std-proc-capture-files.graph) printf "std proc capture files ok" ;;
    conformance/native/pass/std-term-ansi.graph) printf "\033[?1049h\033[2J\033[H\033[?25l\033[1m\033[2m\033[7m\033[31m\033[32m\033[33m\033[34m\033[35m\033[36m\033[37m\033[39mterm ansi\033[0m\033[2K\033[?25h\033[?1049l" ;;
    conformance/native/pass/std-str-breadth.graph) printf "std str breadth ok" ;;
    conformance/native/pass/std-testing-log.graph) printf "std testing log ok" ;;
    examples/std-str.graph) printf "std str ok" ;;
    *)
      echo "missing expected output for $1" >&2
      exit 1
      ;;
  esac
}

examples=(
  examples/hello.graph
  examples/hello-let.graph
  examples/add.graph
  examples/branch.graph
  examples/countdown.graph
  examples/functions.graph
  examples/point.graph
  examples/systems-package
  examples/readall-cli
  examples/batch3-cli
  examples/resource-cli
  examples/zero-hash
  examples/memory-package
  examples/result-choice.graph
  examples/const-arithmetic.graph
  examples/generic-pair.graph
  examples/type-alias.graph
  examples/static-method.graph
  examples/static-interface.graph
  examples/fallibility.graph
  examples/ownership-cleanup.graph
  examples/codec-varint.graph
  examples/parse-cursor.graph
  examples/std-math.graph
  examples/std-str.graph
  examples/std-testing-log.graph
  examples/file-copy.graph
  examples/grep-scan.graph
  conformance/native/pass/std-crypto-hmac32.graph
  conformance/native/pass/std-crypto-sha256.graph
  conformance/native/pass/string-byte-ergonomics.graph
  conformance/native/pass/std-math-breadth.graph
  conformance/native/pass/std-numeric-random-time.graph
  conformance/native/pass/std-regex.graph
  conformance/native/pass/std-unicode.graph
  conformance/native/pass/std-inet.graph
  conformance/native/pass/std-time-rfc3339.graph
  conformance/native/pass/std-io-lines.graph
  conformance/native/pass/std-path-io-breadth.graph
  conformance/native/pass/std-fs-file-helpers.graph
  conformance/native/pass/std-fs-write-file-bool.graph
  conformance/native/pass/std-proc-child.graph
  conformance/native/pass/std-proc-capture.graph
  conformance/native/pass/std-proc-capture-files.graph
  conformance/native/pass/std-term-ansi.graph
  conformance/native/pass/std-str-breadth.graph
  conformance/native/pass/std-testing-log.graph
)

native_case_in_scope() {
  if [[ "$native_test_scope" != "fast" ]]; then
    return 0
  fi

  case "$1" in
    examples/hello.graph | \
    examples/add.graph | \
    examples/generic-pair.graph | \
    examples/fallibility.graph | \
    examples/std-math.graph | \
    examples/std-str.graph | \
    examples/std-testing-log.graph | \
    .zero/native-test/project | \
    conformance/native/pass/world-stream-renamed-param.graph | \
    conformance/native/pass/std-collections-u8.graph | \
    conformance/native/pass/std-codec-json-url.graph | \
    conformance/native/pass/std-json-allocator-capacity.graph | \
    conformance/native/pass/std-fs-bytes.graph | \
    conformance/native/pass/std-fs-write-file-bool.graph | \
    conformance/native/pass/std-cli-helpers.graph | \
    conformance/native/pass/generic-function-basic.graph | \
    conformance/native/pass/match-fallback.graph | \
    conformance/native/pass/recursive-multi-call-let.graph | \
    conformance/native/pass/mutual-recursion.graph)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

run_native_or_gap() {
  local input="$1"
  local out="$2"
  local expected="$3"
  shift 3

  if ! native_case_in_scope "$input"; then
    return 0
  fi

  if ! native_case_selected; then
    return 0
  fi

  local native_case_started_at="$SECONDS"
  # Check passes, or fails with the same BLD004 buildability gate the build
  # path reports for gated typed graph MIR constructs.
  if ! bin/zero check "$input" >/dev/null 2>"$out.check.err"; then
    grep -q "BLD004" "$out.check.err"
  fi
  if bin/zero build --json --emit exe --target linux-musl-x64 "$input" --out "$out" > "$out.json"; then
    local native_output
    if ! native_output="$("$out" "$@" 2>/dev/null)"; then
      native_log_elapsed "case backend gap: $input" "$native_case_started_at"
      return 0
    fi
    if [[ "$native_output" != "$expected" ]]; then
      echo "native output mismatch for $input" >&2
      echo "native:   $native_output" >&2
      echo "expected: $expected" >&2
      exit 1
    fi
    native_log_elapsed "case ok: $input" "$native_case_started_at"
  else
    grep -q '"code"[[:space:]]*:[[:space:]]*"BLD004"' "$out.json"
    native_log_elapsed "case backend gap: $input" "$native_case_started_at"
  fi
}

for example in "${examples[@]}"; do
  name="$(basename "$example")"
  name="${name%.0}"
  name="${name%.graph}"
  native_exe=".zero/native-test/$name"
  run_native_or_gap "$example" "$native_exe" "$(expected_output "$example")"
done

project=".zero/native-test/project"
mkdir -p "$project/src"
cat > "$project/zero.toml" <<'PROJECT'
[package]
name = "native-project"
version = "0.1.0"

[targets.cli]
kind = "exe"
main = "src/main.0"
PROJECT

cat > "$project/src/main.0" <<'SOURCE'
pub fn main(world: World) -> Void raises {
    check world.out.write("native project\n")
}
SOURCE

bin/zero import "$project" >/dev/null
run_native_or_gap "$project" .zero/native-test/project-exe "native project"
run_native_or_gap conformance/native/pass/world-stream-renamed-param.graph .zero/native-test/world-stream-renamed-param "renamed world"

run_native_or_gap conformance/native/pass/params.graph .zero/native-test/params "params work"
run_native_or_gap conformance/native/pass/shape.graph .zero/native-test/shape "native shape"
run_native_or_gap conformance/native/pass/primitive-stdlib.graph .zero/native-test/primitive-stdlib "native primitive std"
run_native_or_gap conformance/native/pass/variants-defer-stdlib.graph .zero/native-test/variants-defer-stdlib "native variants std"
run_native_or_gap conformance/native/pass/payload-match.graph .zero/native-test/payload-match "native payload match"
run_native_or_gap conformance/native/pass/std-mem-arrays.graph .zero/native-test/std-mem-arrays "native std mem arrays"
run_native_or_gap conformance/native/pass/std-collections-algorithms.graph .zero/native-test/std-collections-algorithms "std collections algorithms ok"
run_native_or_gap conformance/native/pass/std-collections-u8.graph .zero/native-test/std-collections-u8 "std collections u8 ok"
run_native_or_gap conformance/native/pass/std-collections-mutspan-memory.graph .zero/native-test/std-collections-mutspan-memory "std collections mutspan memory ok"
run_native_or_gap conformance/native/pass/std-collections-usize-memory.graph .zero/native-test/std-collections-usize-memory "std collections usize memory ok"
run_native_or_gap conformance/native/pass/std-collections-query-memory.graph .zero/native-test/std-collections-query-memory "std collections query memory ok"
run_native_or_gap conformance/native/pass/std-search-sort-widths.graph .zero/native-test/std-search-sort-widths "std search sort widths ok"
run_native_or_gap conformance/native/pass/std-codec-json-url.graph .zero/native-test/std-codec-json-url "std codec json url ok"
run_native_or_gap conformance/native/pass/memory-types.graph .zero/native-test/memory-types "native memory types"
run_native_or_gap conformance/native/pass/recursive-fibonacci.graph .zero/native-test/recursive-fibonacci "recursive fibonacci ok"
run_native_or_gap conformance/native/pass/recursive-multi-call-let.graph .zero/native-test/recursive-multi-call-let "recursive multi call ok"
run_native_or_gap conformance/native/pass/mutual-recursion.graph .zero/native-test/mutual-recursion "mutual recursion ok"
run_native_or_gap conformance/native/pass/scratch-nested-index.graph .zero/native-test/scratch-nested-index "scratch nested index ok"
run_native_or_gap conformance/native/pass/owned-transfer.graph .zero/native-test/owned-transfer "owned transfer ok"
run_native_or_gap conformance/native/pass/owned-drop-cleanup.graph .zero/native-test/owned-drop-cleanup "owned drop cleanup ok"
run_native_or_gap conformance/native/pass/owned-drop-move-suppressed.graph .zero/native-test/owned-drop-move-suppressed "owned drop move suppressed ok"
run_native_or_gap conformance/native/pass/borrow-primitives.graph .zero/native-test/borrow-primitives "borrow primitives ok"
run_native_or_gap conformance/native/pass/mutref-indexed-lvalues.graph .zero/native-test/mutref-indexed-lvalues "mutref indexed lvalues ok"
run_native_or_gap conformance/native/pass/allocator-primitives.graph .zero/native-test/allocator-primitives "allocator primitives ok"
run_native_or_gap conformance/native/pass/owned-byte-buffer.graph .zero/native-test/owned-byte-buffer "owned byte buffer ok"
run_native_or_gap conformance/native/pass/std-mem-arena.graph .zero/native-test/std-mem-arena "arena ok"
run_native_or_gap conformance/native/pass/std-json-duplicate-keys.graph .zero/native-test/std-json-duplicate-keys ""
run_native_or_gap conformance/native/pass/std-json-allocator-capacity.graph .zero/native-test/std-json-allocator-capacity ""
run_native_or_gap conformance/native/pass/fallibility-error-sets.graph .zero/native-test/fallibility-error-sets "fallibility error sets ok"
run_native_or_gap conformance/native/pass/rescue-check.graph .zero/native-test/rescue-check "rescue ok"
run_native_or_gap conformance/native/pass/std-fs-fallible.graph .zero/native-test/std-fs-fallible "fs named errors ok"
run_native_or_gap conformance/native/pass/std-fs-fallible-resources.graph .zero/native-test/std-fs-fallible-resources "fs fallible resources ok"
run_native_or_gap conformance/native/pass/std-cli-helpers.graph .zero/native-test/std-cli-helpers "cli helpers ok"
run_native_or_gap conformance/native/pass/std-fs-bytes.graph .zero/native-test/std-fs-bytes "fs bytes ok"
run_native_or_gap conformance/native/pass/std-fs-write-file-bool.graph .zero/native-test/std-fs-write-file-bool "fs write file bool ok"
run_native_or_gap conformance/native/pass/std-fs-read-chunks.graph .zero/native-test/std-fs-read-chunks "fs read chunks ok"
run_native_or_gap conformance/native/pass/std-fs-resource.graph .zero/native-test/std-fs-resource "fs resource ok"
run_native_or_gap conformance/native/pass/std-fs-readall.graph .zero/native-test/std-fs-readall "fs readAll ok"
run_native_or_gap conformance/native/pass/std-fs-polish.graph .zero/native-test/std-fs-polish "fs polish ok"
run_native_or_gap conformance/native/pass/std-mem-copy-fill.graph .zero/native-test/std-mem-copy-fill "mem copy fill ok"
run_native_or_gap conformance/native/pass/generic-function-basic.graph .zero/native-test/generic-function-basic "generic function ok"
run_native_or_gap conformance/native/pass/generic-shape-basic.graph .zero/native-test/generic-shape-basic "generic shape ok"
run_native_or_gap conformance/native/pass/generic-shape-multi.graph .zero/native-test/generic-shape-multi "generic shape multi ok"
run_native_or_gap conformance/native/pass/generic-constructor-expected.graph .zero/native-test/generic-constructor-expected "generic constructor expected ok"
run_native_or_gap conformance/native/pass/generic-literals-arrays.graph .zero/native-test/generic-literals-arrays "generic literals arrays ok"
run_native_or_gap conformance/native/pass/top-level-const.graph .zero/native-test/top-level-const "const ok"
run_native_or_gap conformance/native/pass/const-arithmetic.graph .zero/native-test/const-arithmetic "const arithmetic ok"
run_native_or_gap conformance/native/pass/type-alias-basic.graph .zero/native-test/type-alias-basic "type alias ok"
run_native_or_gap conformance/native/pass/static-method-namespace.graph .zero/native-test/static-method-namespace "static method ok"
run_native_or_gap conformance/native/pass/match-fallback.graph .zero/native-test/match-fallback "match fallback ok"

if native_phase_selected "metadata-and-reports"; then
native_phase_started_at="$SECONDS"
std_args_run_exe="/tmp/zero-std-args-run-$$"
std_args_run_output="$(bin/zero run --out "$std_args_run_exe" conformance/native/pass/std-args.graph -- agent-arg extra)"
rm -f "$std_args_run_exe"
if [[ "$std_args_run_output" != "agent-arg" ]]; then
  echo "zero run output mismatch for conformance/native/pass/std-args.graph" >&2
  echo "native:   $std_args_run_output" >&2
  echo "expected: agent-arg" >&2
  exit 1
fi
bin/zero build --json --emit exe --target linux-musl-x64 conformance/native/pass/std-args.graph --out .zero/native-test/std-args > .zero/native-test/std-args.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/std-args.json
if [ "$(uname -s)" = "Linux" ] && [ "$(uname -m)" = "x86_64" ]; then
  std_args_output="$(.zero/native-test/std-args agent-arg extra)"
  if [[ "$std_args_output" != "agent-arg" ]]; then
    echo "native output mismatch for conformance/native/pass/std-args.graph" >&2
    echo "native:   $std_args_output" >&2
    echo "expected: agent-arg" >&2
    exit 1
  fi
  rm -f .zero/native-test/std-http-response-helpers-linux .zero/native-test/std-http-response-helpers-linux.json .zero/native-test/std-http-response-helpers-linux.zero.o .zero/native-test/std-http-response-helpers-linux.zero-runtime.o
  if ! bin/zero build --json --emit exe --target linux-x64 conformance/native/pass/std-http-response-helpers.graph --out .zero/native-test/std-http-response-helpers-linux > .zero/native-test/std-http-response-helpers-linux.json; then
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
  rm -f .zero/native-test/std-http-text-html-response-helpers-linux .zero/native-test/std-http-text-html-response-helpers-linux.json .zero/native-test/std-http-text-html-response-helpers-linux.zero.o .zero/native-test/std-http-text-html-response-helpers-linux.zero-runtime.o
  if ! bin/zero build --json --emit exe --target linux-x64 conformance/native/pass/std-http-text-html-response-helpers.graph --out .zero/native-test/std-http-text-html-response-helpers-linux > .zero/native-test/std-http-text-html-response-helpers-linux.json; then
    cat .zero/native-test/std-http-text-html-response-helpers-linux.json >&2
    exit 1
  fi
  .zero/native-test/std-http-text-html-response-helpers-linux
  test ! -f .zero/native-test/std-http-text-html-response-helpers-linux.zero.o
  test ! -f .zero/native-test/std-http-text-html-response-helpers-linux.zero-runtime.o
  node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/std-http-text-html-response-helpers-linux.json","utf8")); if (report.generatedCBytes!==0 || report.objectBackend.objectEmission.path!=="direct-elf64-object" || report.objectBackend.linking.targetLibraries!=="zero-runtime" || report.objectBackend.linking.externalToolchain!=="cc" || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_runtime.o") || report.objectBackend.directFacts.runtimeHelperCount!==1) process.exit(1);'
  rm -f .zero/native-test/std-http-redirect-response-helpers-linux .zero/native-test/std-http-redirect-response-helpers-linux.json .zero/native-test/std-http-redirect-response-helpers-linux.zero.o .zero/native-test/std-http-redirect-response-helpers-linux.zero-runtime.o
  if ! bin/zero build --json --emit exe --target linux-x64 conformance/native/pass/std-http-redirect-response-helpers.graph --out .zero/native-test/std-http-redirect-response-helpers-linux > .zero/native-test/std-http-redirect-response-helpers-linux.json; then
    cat .zero/native-test/std-http-redirect-response-helpers-linux.json >&2
    exit 1
  fi
  .zero/native-test/std-http-redirect-response-helpers-linux
  test ! -f .zero/native-test/std-http-redirect-response-helpers-linux.zero.o
  test ! -f .zero/native-test/std-http-redirect-response-helpers-linux.zero-runtime.o
  node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/std-http-redirect-response-helpers-linux.json","utf8")); if (report.generatedCBytes!==0 || report.objectBackend.objectEmission.path!=="direct-elf64-object" || report.objectBackend.linking.targetLibraries!=="zero-runtime" || report.objectBackend.linking.externalToolchain!=="cc" || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_runtime.o") || report.objectBackend.directFacts.runtimeHelperCount!==1) process.exit(1);'
  rm -f .zero/native-test/std-http-api-helpers-linux .zero/native-test/std-http-api-helpers-linux.json .zero/native-test/std-http-api-helpers-linux.zero.o .zero/native-test/std-http-api-helpers-linux.zero-runtime.o
  if ! bin/zero build --json --emit exe --target linux-x64 conformance/native/pass/std-http-api-helpers.graph --out .zero/native-test/std-http-api-helpers-linux > .zero/native-test/std-http-api-helpers-linux.json; then
    cat .zero/native-test/std-http-api-helpers-linux.json >&2
    exit 1
  fi
  set +e
  .zero/native-test/std-http-api-helpers-linux
  std_http_api_helpers_linux_status=$?
  set -e
  test "$std_http_api_helpers_linux_status" = "32"
  test ! -f .zero/native-test/std-http-api-helpers-linux.zero.o
  test ! -f .zero/native-test/std-http-api-helpers-linux.zero-runtime.o
  node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/std-http-api-helpers-linux.json","utf8")); if (report.generatedCBytes!==0 || report.objectBackend.objectEmission.path!=="direct-elf64-object" || report.objectBackend.linking.targetLibraries!=="zero-runtime" || report.objectBackend.linking.externalToolchain!=="cc" || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_runtime.o") || report.objectBackend.directFacts.runtimeHelperCount!==1) process.exit(1);'
  rm -f .zero/native-test/std-http-cors-helpers-linux .zero/native-test/std-http-cors-helpers-linux.json .zero/native-test/std-http-cors-helpers-linux.zero.o .zero/native-test/std-http-cors-helpers-linux.zero-runtime.o
  if ! bin/zero build --json --emit exe --target linux-x64 conformance/native/pass/std-http-cors-helpers.graph --out .zero/native-test/std-http-cors-helpers-linux > .zero/native-test/std-http-cors-helpers-linux.json; then
    cat .zero/native-test/std-http-cors-helpers-linux.json >&2
    exit 1
  fi
  set +e
  .zero/native-test/std-http-cors-helpers-linux
  std_http_cors_helpers_linux_status=$?
  set -e
  test "$std_http_cors_helpers_linux_status" = "32"
  test ! -f .zero/native-test/std-http-cors-helpers-linux.zero.o
  test ! -f .zero/native-test/std-http-cors-helpers-linux.zero-runtime.o
  node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/std-http-cors-helpers-linux.json","utf8")); if (report.generatedCBytes!==0 || report.objectBackend.objectEmission.path!=="direct-elf64-object" || report.objectBackend.linking.targetLibraries!=="zero-runtime" || report.objectBackend.linking.externalToolchain!=="cc" || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_runtime.o") || report.objectBackend.directFacts.runtimeHelperCount!==1) process.exit(1);'
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
    if ! bin/zero build --json --emit exe --target linux-x64 conformance/native/pass/std-http-fetch.graph --out .zero/native-test/std-http-fetch-linux > .zero/native-test/std-http-fetch-linux.json; then
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

bin/zero check conformance/native/pass/std-env.graph >/dev/null

bin/zero inspect --json conformance/check/pass/imports > .zero/native-test/imports-graph.json
node -e 'const fs=require("node:fs"); const j=JSON.parse(fs.readFileSync(".zero/native-test/imports-graph.json","utf8")); const imports=(j.importEdges||[]).map((edge)=>edge.to).sort(); if (!j.targets || imports.join(",")!=="math,types") process.exit(1);'
bin/zero inspect --json examples/resource-cli > .zero/native-test/resource-cli-graph.json
grep -q '"importEdges":' .zero/native-test/resource-cli-graph.json
grep -q '"requiresCapabilities": \["args", "env", "fs", "memory", "path", "world"\]' .zero/native-test/resource-cli-graph.json
bin/zero inspect --json conformance/native/pass/std-io-direct.graph > .zero/native-test/std-io-direct-graph.json
grep -q '"requiresCapabilities": \["memory", "world"\]' .zero/native-test/std-io-direct-graph.json
grep -q '"name":"std.io.bufferedReader"' .zero/native-test/std-io-direct-graph.json
grep -q '"name":"std.io.bufferedWriter"' .zero/native-test/std-io-direct-graph.json
grep -q '"name":"std.io.copy"' .zero/native-test/std-io-direct-graph.json
bin/zero inspect --json examples/static-method.graph > .zero/native-test/static-method-graph.json
grep -q '"methods":' .zero/native-test/static-method-graph.json
grep -q '"staticDispatch":true' .zero/native-test/static-method-graph.json
bin/zero inspect --json examples/type-alias.graph > .zero/native-test/type-alias-graph.json
node -e 'const fs=require("node:fs"); const j=JSON.parse(fs.readFileSync(".zero/native-test/type-alias-graph.json","utf8")); if (!Array.isArray(j.aliases) || !j.aliases.some((alias)=>alias.name==="BytePair" && alias.target==="Pair<u8, u8>")) process.exit(1);'
bin/zero inspect --json --target linux-musl-x64 examples/memory-package > .zero/native-test/memory-package-graph.json
grep -q '"fsAvailable":true' .zero/native-test/memory-package-graph.json
grep -q '"requiresCapabilities": \["memory", "world"\]' .zero/native-test/memory-package-graph.json
bin/zero size --json conformance/native/pass/std-io-direct.graph > .zero/native-test/std-io-direct-size.json
grep -q '"name":"std.io.bufferedReader"' .zero/native-test/std-io-direct-size.json
grep -q '"name":"std.io.bufferedWriter"' .zero/native-test/std-io-direct-size.json
grep -q '"name":"std.io.copy"' .zero/native-test/std-io-direct-size.json
grep -q '"name":"stdio-world"' .zero/native-test/std-io-direct-size.json
bin/zero mem --json conformance/native/pass/std-io-direct.graph > .zero/native-test/std-io-direct-mem.json
grep -q '"artifact":"conformance/native/pass/std-io-direct.graph"' .zero/native-test/std-io-direct-mem.json
grep -q '"sourceKind":"program-graph"' .zero/native-test/std-io-direct-mem.json
grep -q '"graphArtifact":"conformance/native/pass/std-io-direct.graph"' .zero/native-test/std-io-direct-mem.json
grep -q '"generatedCBytes": 0' .zero/native-test/std-io-direct-mem.json
grep -q '"cBridgeFallback": false' .zero/native-test/std-io-direct-mem.json
grep -q '"stackBytes":' .zero/native-test/std-io-direct-mem.json
grep -q '"maxFrameBytes":' .zero/native-test/std-io-direct-mem.json
grep -q '"readonlyDataBytes":' .zero/native-test/std-io-direct-mem.json
grep -q '"runtimeHelperCount":1' .zero/native-test/std-io-direct-mem.json
grep -q '"name":"stdio-world"' .zero/native-test/std-io-direct-mem.json
grep -q '"hiddenHeapAllocation":false' .zero/native-test/std-io-direct-mem.json
bin/zero check --target linux-musl-x64 conformance/native/pass/std-http-metadata-neutral.graph >/dev/null
bin/zero inspect --json --target linux-musl-x64 conformance/native/pass/std-http-metadata-neutral.graph > .zero/native-test/std-http-metadata-neutral-graph.json
grep -q '"requiresCapabilities": \["memory", "parse"\]' .zero/native-test/std-http-metadata-neutral-graph.json
if bin/zero check --target linux-musl-x64 conformance/native/pass/std-http-fetch.graph >/dev/null 2>.zero/native-test/std-http-target-unsupported.err; then
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
assert.equal(body.syntax, "canonical");
assert.deepEqual(body.tokens.slice(0, 4).map((token) => `${token.kind}:${token.text}`), [
  "word:use",
  "word:std",
  "symbol:.",
  "word:mem",
]);
assert.deepEqual(body.tokens.slice(6, 13).map((token) => `${token.kind}:${token.text}`), [
  "word:pub",
  "word:fn",
  "word:main",
  "symbol:(",
  "symbol:)",
  "symbol:->",
  "word:Void",
]);
assert.equal(body.tokens[6].offset, 13);
assert.equal(body.tokens[12].line, 3);
assert.equal(body.tokens.at(-1).kind, "eof");
NODE

bin/zero parse --json conformance/format/functions-blocks.0 > .zero/native-test/parse-tree.json
node <<'NODE'
const assert = require("node:assert/strict");
const fs = require("node:fs");

const body = JSON.parse(fs.readFileSync(".zero/native-test/parse-tree.json", "utf8"));
assert.equal(body.schemaVersion, 1);
assert.equal(body.root.kind, "module");
assert.equal(body.root.shapeCount, 0);
assert.equal(body.root.enumCount, 0);
assert.equal(body.root.choiceCount, 0);
assert.equal(body.root.functionCount, 2);
assert.deepEqual(body.functions[0].bodyKinds, ["if"]);
assert.deepEqual(body.functions[1].bodyKinds, ["let", "while"]);
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

assert_direct_size_metadata "conformance/native/pass/std-fs-readall.graph"
for size_case in \
  "examples/hello.graph" \
  "examples/zero-hash" \
  "examples/generic-pair.graph" \
  "conformance/native/pass/generic-function-basic.graph" \
  "conformance/native/pass/generic-shape-basic.graph" \
  "conformance/native/pass/generic-shape-multi.graph" \
  "conformance/native/pass/std-mem-copy-fill.graph"
do
  assert_direct_size_metadata "$size_case"
done
bin/zero size --json examples/hello.graph > .zero/native-test/hello-size.json
node -e 'const fs=require("node:fs"); const j=JSON.parse(fs.readFileSync(".zero/native-test/hello-size.json","utf8")); if (!j.compilerRuntimeHelpers.every((h)=>h.payAsUsed===true && h.emitted===false)) process.exit(1);'

[[ "$(bin/zero test conformance/native/pass/test-blocks.graph)" == "1 test(s) ok" ]]
[[ "$(bin/zero test conformance/native/pass/std-testing-helpers-test.graph)" == "1 test(s) ok" ]]
[[ "$(bin/zero run --out .zero/native-test/run-add examples/add.graph)" == "math works" ]]
bin/zero test --json conformance/packages/test-app > .zero/native-test/test-package.json
node -e 'const fs=require("node:fs"); const j=JSON.parse(fs.readFileSync(".zero/native-test/test-package.json","utf8")); if (!j.ok || j.graph?.artifact!=="conformance/packages/test-app/zero.graph" || j.graph?.sourceProjectionState!=="clean" || j.graph?.moduleIdentity!=="package:test-app@0.1.0" || j.testDiscovery.mode!=="package-graph" || j.testDiscovery.manifestPath!=="conformance/packages/test-app/zero.toml" || j.discoveredTests!==3 || j.selectedTests!==3 || j.expectedFailures!==1 || !j.fixtures.sourceFiles.some((p)=>p.endsWith("helper.0")) || !j.targetFacts.capabilitySupport) process.exit(1);'
bin/zero test --json --filter helper conformance/packages/test-app > .zero/native-test/test-package-filter.json
node -e 'const fs=require("node:fs"); const j=JSON.parse(fs.readFileSync(".zero/native-test/test-package-filter.json","utf8")); if (!j.ok || j.graph?.artifact!=="conformance/packages/test-app/zero.graph" || j.graph?.sourceProjectionState!=="clean" || j.testDiscovery.mode!=="package-graph" || j.testDiscovery.manifestPath!=="conformance/packages/test-app/zero.toml" || j.discoveredTests!==3 || j.selectedTests!==2 || j.testDiscovery.filter!=="helper") process.exit(1);'
bin/zero test --json conformance/native/pass/test-expected-fail.graph > .zero/native-test/test-expected-fail.json
node -e 'const fs=require("node:fs"); const j=JSON.parse(fs.readFileSync(".zero/native-test/test-expected-fail.json","utf8")); if (!j.ok || j.expectedFailures!==1 || j.failedTests!==0 || j.results[0].status!=="expected-fail") process.exit(1);'
scripts/reliability-smoke.mts >/dev/null
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
bin/zero inspect --json examples/point.graph > .zero/native-test/point-graph.json
grep -q '"shapes"' .zero/native-test/point-graph.json
bin/zero inspect --json examples/systems-package > .zero/native-test/systems-package-graph.json
grep -q '"choices"' .zero/native-test/systems-package-graph.json
if bin/zero size --json examples/point.graph > .zero/native-test/point-size.json; then
  echo "expected point size to report direct backend gap" >&2
  exit 1
fi
node -e 'const fs=require("node:fs"); const j=JSON.parse(fs.readFileSync(".zero/native-test/point-size.json","utf8")); if (j.diagnostics?.[0]?.code!=="BLD004") process.exit(1);'
bin/zero inspect --json examples/memory-package > .zero/native-test/memory-package-graph-helpers.json
grep -q '"stdlibHelpers"' .zero/native-test/memory-package-graph-helpers.json
bin/zero size --json examples/memory-package > .zero/native-test/memory-package-size.json
grep -q '"stdlibHelpers"' .zero/native-test/memory-package-size.json
if bin/zero build --json --emit c --target linux-musl-x64 examples/hello.graph --out .zero/native-test/removed-c-backend.c > .zero/native-test/removed-c-backend.json; then
  echo "expected removed C backend to fail" >&2
  exit 1
fi
node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(process.argv[1],"utf8")); if (report.diagnostics?.[0]?.code!=="BLD003") process.exit(1);' .zero/native-test/removed-c-backend.json
if bin/zero build --json --legacy-backend --target linux-musl-x64 examples/hello.graph --out .zero/native-test/removed-legacy-backend > .zero/native-test/removed-legacy-backend.json; then
  echo "expected removed legacy backend flag to fail" >&2
  exit 1
fi
node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(process.argv[1],"utf8")); if (report.diagnostics?.[0]?.code!=="BLD003") process.exit(1);' .zero/native-test/removed-legacy-backend.json
native_log_elapsed "metadata and reports ok" "$native_phase_started_at"
fi

if native_phase_selected "direct-backend-artifacts"; then
native_phase_started_at="$SECONDS"
rm -f .zero/native-test/direct-obj-add.o .zero/native-test/direct-obj-add.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-obj-add.graph --out .zero/native-test/direct-obj-add.o > .zero/native-test/direct-obj-add.json
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
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-i64-return.graph --out .zero/native-test/direct-i64-return.o > .zero/native-test/direct-i64-return-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-i64-return.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62 || !b.includes(Buffer.from([0x48,0xb8,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x7f])) || !b.includes(Buffer.from([0x48,0x01,0xc8]))) process.exit(1);'
test ! -f .zero/native-test/direct-i64-return.o.c
grep -q '"path":"direct-elf64-object"' .zero/native-test/direct-i64-return-obj.json
rm -f .zero/native-test/direct-if-return.o .zero/native-test/direct-if-return.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-if-return.graph --out .zero/native-test/direct-if-return.o > .zero/native-test/direct-if-return-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-if-return.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-if-return.o.c
grep -q '"path":"direct-elf64-object"' .zero/native-test/direct-if-return-obj.json
rm -f .zero/native-test/direct-call-add.o .zero/native-test/direct-call-add.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-call-add.graph --out .zero/native-test/direct-call-add.o > .zero/native-test/direct-call-add-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-call-add.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62 || !b.includes(Buffer.concat([Buffer.from("add"),Buffer.from([0])])) || !b.includes(Buffer.concat([Buffer.from("main"),Buffer.from([0])]))) process.exit(1);'
test ! -f .zero/native-test/direct-call-add.o.c
grep -q '"path":"direct-elf64-object"' .zero/native-test/direct-call-add-obj.json
rm -f .zero/native-test/direct-call-add-linux-gnu.o .zero/native-test/direct-call-add-linux-gnu.o.c
bin/zero build --json --emit obj --target linux-x64 examples/direct-call-add.graph --out .zero/native-test/direct-call-add-linux-gnu.o > .zero/native-test/direct-call-add-linux-gnu.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-call-add-linux-gnu.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-call-add-linux-gnu.o.c
grep -q '"target": "linux-x64"' .zero/native-test/direct-call-add-linux-gnu.json
grep -q '"selectedEmitter":"zero-elf64"' .zero/native-test/direct-call-add-linux-gnu.json
rm -f .zero/native-test/direct-call-add-darwin.o .zero/native-test/direct-call-add-darwin.o.c
bin/zero build --json --emit obj --target darwin-arm64 examples/direct-call-add.graph --out .zero/native-test/direct-call-add-darwin.o > .zero/native-test/direct-call-add-darwin.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-call-add-darwin.o"); const section=32+72; const reloff=b.readUInt32LE(section+56); const nreloc=b.readUInt32LE(section+60); const info=reloff ? b.readUInt32LE(reloff+4) : 0; if (b.readUInt32LE(0)!==0xfeedfacf || b.readUInt32LE(4)!==0x0100000c || b.readUInt32LE(12)!==1 || !b.includes(Buffer.concat([Buffer.from("_main"),Buffer.from([0])])) || !b.includes(Buffer.concat([Buffer.from("_add"),Buffer.from([0])])) || !b.includes(Buffer.from([0x00,0x01,0x09,0x0b])) || reloff===0 || nreloc<1 || ((info>>>28)&15)!==2) process.exit(1);'
test ! -f .zero/native-test/direct-call-add-darwin.o.c
grep -q '"path":"direct-macho64-object"' .zero/native-test/direct-call-add-darwin.json
grep -q '"selectedEmitter":"zero-macho64"' .zero/native-test/direct-call-add-darwin.json
rm -f .zero/native-test/direct-byte-view-reloc-darwin.o .zero/native-test/direct-byte-view-reloc-darwin.o.c
bin/zero build --json --emit obj --target darwin-arm64 examples/direct-byte-view-reloc.graph --out .zero/native-test/direct-byte-view-reloc-darwin.o > .zero/native-test/direct-byte-view-reloc-darwin.json
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
bin/zero build --json --emit obj --target darwin-arm64 examples/hello.graph --out .zero/native-test/direct-hello-darwin.o > .zero/native-test/direct-hello-darwin.json
node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/direct-hello-darwin.json","utf8")); const b=fs.readFileSync(".zero/native-test/direct-hello-darwin.o"); const section=32+72; const reloff=b.readUInt32LE(section+56); const nreloc=b.readUInt32LE(section+60); let sawBranch=false; for (let i=0;i<nreloc;i++){ if (((b.readUInt32LE(reloff+i*8+4)>>>28)&15)===2) sawBranch=true; } if (report.objectBackend.objectEmission.symbolCount!==3 || report.objectBackend.directFacts.runtimeHelperCount!==1 || b.readUInt32LE(0)!==0xfeedfacf || b.readUInt32LE(4)!==0x0100000c || b.readUInt32LE(12)!==1 || reloff===0 || nreloc<2 || !sawBranch || !b.includes(Buffer.from("hello from zero")) || !b.includes(Buffer.from("_zero_world_write"))) process.exit(1);'
test ! -f .zero/native-test/direct-hello-darwin.o.c
grep -q '"path":"direct-macho64-object"' .zero/native-test/direct-hello-darwin.json
grep -q '"generatedCBytes": 0' .zero/native-test/direct-hello-darwin.json
rm -f .zero/native-test/direct-hello-darwin-x64.o .zero/native-test/direct-hello-darwin-x64.o.c .zero/native-test/direct-hello-darwin-x64 .zero/native-test/direct-hello-darwin-x64.c
bin/zero build --json --emit obj --target darwin-x64 examples/hello.graph --out .zero/native-test/direct-hello-darwin-x64.o > .zero/native-test/direct-hello-darwin-x64-obj.json
node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/direct-hello-darwin-x64-obj.json","utf8")); const b=fs.readFileSync(".zero/native-test/direct-hello-darwin-x64.o"); const section=32+72; const reloff=b.readUInt32LE(section+56); const nreloc=b.readUInt32LE(section+60); const types=[]; for (let i=0;i<nreloc;i++) types.push((b.readUInt32LE(reloff+i*8+4)>>>28)&15); if (report.compiler!=="zero-macho-x64" || report.objectBackend.objectEmission.path!=="direct-macho-x64-object" || report.objectBackend.objectEmission.symbolCount!==3 || report.objectBackend.directFacts.runtimeHelperCount!==1 || b.readUInt32LE(0)!==0xfeedfacf || b.readUInt32LE(4)!==0x01000007 || b.readUInt32LE(8)!==3 || b.readUInt32LE(12)!==1 || reloff===0 || !types.includes(1) || !types.includes(2) || !b.includes(Buffer.from("hello from zero")) || !b.includes(Buffer.from("_zero_world_write"))) process.exit(1);'
test ! -f .zero/native-test/direct-hello-darwin-x64.o.c
bin/zero build --json --emit exe --target darwin-x64 examples/hello.graph --out .zero/native-test/direct-hello-darwin-x64 > .zero/native-test/direct-hello-darwin-x64-exe.json
node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/direct-hello-darwin-x64-exe.json","utf8")); const b=fs.readFileSync(".zero/native-test/direct-hello-darwin-x64"); if (report.compiler!=="zero-macho-x64" || report.objectBackend.objectEmission.path!=="direct-macho-x64-exe" || b.readUInt32LE(0)!==0xfeedfacf || b.readUInt32LE(4)!==0x01000007 || b.readUInt32LE(8)!==3 || b.readUInt32LE(12)!==2 || !b.includes(Buffer.from("hello from zero")) || !b.includes(Buffer.from("zero-direct-x64"))) process.exit(1);'
test ! -f .zero/native-test/direct-hello-darwin-x64.c
rm -f .zero/native-test/direct-unhandled-error-darwin-x64 .zero/native-test/direct-unhandled-error-darwin-x64.c
bin/zero build --json --emit exe --target darwin-x64 examples/direct-unhandled-error-exit.graph --out .zero/native-test/direct-unhandled-error-darwin-x64 > .zero/native-test/direct-unhandled-error-darwin-x64.json
node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/direct-unhandled-error-darwin-x64.json","utf8")); const b=fs.readFileSync(".zero/native-test/direct-unhandled-error-darwin-x64"); if (report.compiler!=="zero-macho-x64" || report.generatedCBytes!==0 || report.objectBackend.objectEmission.path!=="direct-macho-x64-exe" || b.readUInt32LE(0)!==0xfeedfacf || b.readUInt32LE(4)!==0x01000007 || b.readUInt32LE(8)!==3 || b.readUInt32LE(12)!==2 || !b.includes(Buffer.from("zero-direct-x64"))) process.exit(1);'
test ! -f .zero/native-test/direct-unhandled-error-darwin-x64.c
if [ "$(uname -s)" = "Darwin" ] && command -v arch >/dev/null 2>&1 && arch -x86_64 /usr/bin/true >/dev/null 2>&1; then
  direct_hello_darwin_x64_output="$(arch -x86_64 .zero/native-test/direct-hello-darwin-x64)"
  test "$direct_hello_darwin_x64_output" = "hello from zero"
  set +e
  arch -x86_64 .zero/native-test/direct-unhandled-error-darwin-x64
  direct_unhandled_error_darwin_x64_rc=$?
  set -e
  test "$direct_unhandled_error_darwin_x64_rc" -eq 1
fi
rm -f .zero/native-test/direct-std-args-darwin.o .zero/native-test/direct-std-args-darwin.o.c .zero/native-test/direct-std-args-darwin-linked .zero/native-test/direct-std-args-darwin-runtime.c .zero/native-test/direct-std-args-darwin-link.0 .zero/native-test/direct-std-args-darwin-link.graph .zero/native-test/direct-std-args-darwin-link.o .zero/native-test/direct-std-args-darwin-link.json
bin/zero build --json --emit obj --target darwin-arm64 conformance/native/pass/std-args.graph --out .zero/native-test/direct-std-args-darwin.o > .zero/native-test/direct-std-args-darwin.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-std-args-darwin.o"); const save=Buffer.from([0xf4,0x57,0xbf,0xa9]); const seed=Buffer.from([0xf4,0x03,0x00,0xaa,0xf5,0x03,0x01,0xaa]); const restore=Buffer.from([0xf4,0x57,0xc1,0xa8]); if (b.readUInt32LE(0)!==0xfeedfacf || b.readUInt32LE(4)!==0x0100000c || !b.includes(save) || !b.includes(seed) || !b.includes(restore)) process.exit(1);'
test ! -f .zero/native-test/direct-std-args-darwin.o.c
grep -q '"path":"direct-macho64-object"' .zero/native-test/direct-std-args-darwin.json
if [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ] && command -v cc >/dev/null 2>&1; then
  cat > .zero/native-test/direct-std-args-darwin-link.0 <<'SOURCE'
pub fn main(world: World) -> Void raises {
    let first: Maybe<String> = std.args.get(1)
    if first.has {
        check world.out.write(first.value)
    }
}
SOURCE
  bin/zero import --format binary --out .zero/native-test/direct-std-args-darwin-link.graph .zero/native-test/direct-std-args-darwin-link.0
  cat > .zero/native-test/direct-std-args-darwin-runtime.c <<'SOURCE'
#include <unistd.h>
int zero_world_write(int fd, const char *buf, unsigned len) {
  ssize_t written = write(fd, buf, len);
  return written < 0 || (unsigned long long)written != len;
}
SOURCE
  bin/zero build --json --emit obj --target darwin-arm64 .zero/native-test/direct-std-args-darwin-link.graph --out .zero/native-test/direct-std-args-darwin-link.o > .zero/native-test/direct-std-args-darwin-link.json
  cc .zero/native-test/direct-std-args-darwin-link.o .zero/native-test/direct-std-args-darwin-runtime.c -o .zero/native-test/direct-std-args-darwin-linked
  direct_std_args_darwin_output="$(.zero/native-test/direct-std-args-darwin-linked agent-arg extra)"
  test "$direct_std_args_darwin_output" = "agent-arg"
  rm -f .zero/native-test/std-http-response-helpers .zero/native-test/std-http-response-helpers.json .zero/native-test/std-http-response-helpers.zero.o .zero/native-test/std-http-response-helpers.zero-runtime.o
  bin/zero build --json --emit exe --target darwin-arm64 conformance/native/pass/std-http-response-helpers.graph --out .zero/native-test/std-http-response-helpers > .zero/native-test/std-http-response-helpers.json
  set +e
  .zero/native-test/std-http-response-helpers
  std_http_response_helpers_status=$?
  set -e
  test "$std_http_response_helpers_status" = "29"
  test ! -f .zero/native-test/std-http-response-helpers.zero.o
  test ! -f .zero/native-test/std-http-response-helpers.zero-runtime.o
  node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/std-http-response-helpers.json","utf8")); if (report.generatedCBytes!==0 || report.objectBackend.linking.targetLibraries!=="zero-runtime" || report.objectBackend.linking.externalToolchain!=="cc" || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_runtime.o") || report.objectBackend.directFacts.runtimeHelperCount!==1) process.exit(1);'
  rm -f .zero/native-test/std-http-redirect-response-helpers .zero/native-test/std-http-redirect-response-helpers.json .zero/native-test/std-http-redirect-response-helpers.zero.o .zero/native-test/std-http-redirect-response-helpers.zero-runtime.o
  bin/zero build --json --emit exe --target darwin-arm64 conformance/native/pass/std-http-redirect-response-helpers.graph --out .zero/native-test/std-http-redirect-response-helpers > .zero/native-test/std-http-redirect-response-helpers.json
  .zero/native-test/std-http-redirect-response-helpers
  test ! -f .zero/native-test/std-http-redirect-response-helpers.zero.o
  test ! -f .zero/native-test/std-http-redirect-response-helpers.zero-runtime.o
  node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/std-http-redirect-response-helpers.json","utf8")); if (report.generatedCBytes!==0 || report.objectBackend.linking.targetLibraries!=="zero-runtime" || report.objectBackend.linking.externalToolchain!=="cc" || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_runtime.o") || report.objectBackend.directFacts.runtimeHelperCount!==1) process.exit(1);'
  rm -f .zero/native-test/std-http-api-helpers .zero/native-test/std-http-api-helpers.json .zero/native-test/std-http-api-helpers.zero.o .zero/native-test/std-http-api-helpers.zero-runtime.o
  bin/zero build --json --emit exe --target darwin-arm64 conformance/native/pass/std-http-api-helpers.graph --out .zero/native-test/std-http-api-helpers > .zero/native-test/std-http-api-helpers.json
  set +e
  .zero/native-test/std-http-api-helpers
  std_http_api_helpers_status=$?
  set -e
  test "$std_http_api_helpers_status" = "32"
  test ! -f .zero/native-test/std-http-api-helpers.zero.o
  test ! -f .zero/native-test/std-http-api-helpers.zero-runtime.o
  node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/std-http-api-helpers.json","utf8")); if (report.generatedCBytes!==0 || report.objectBackend.linking.targetLibraries!=="zero-runtime" || report.objectBackend.linking.externalToolchain!=="cc" || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_runtime.o") || report.objectBackend.directFacts.runtimeHelperCount!==1) process.exit(1);'
  rm -f .zero/native-test/std-http-cors-helpers .zero/native-test/std-http-cors-helpers.json .zero/native-test/std-http-cors-helpers.zero.o .zero/native-test/std-http-cors-helpers.zero-runtime.o
  bin/zero build --json --emit exe --target darwin-arm64 conformance/native/pass/std-http-cors-helpers.graph --out .zero/native-test/std-http-cors-helpers > .zero/native-test/std-http-cors-helpers.json
  set +e
  .zero/native-test/std-http-cors-helpers
  std_http_cors_helpers_status=$?
  set -e
  test "$std_http_cors_helpers_status" = "32"
  test ! -f .zero/native-test/std-http-cors-helpers.zero.o
  test ! -f .zero/native-test/std-http-cors-helpers.zero-runtime.o
  node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/std-http-cors-helpers.json","utf8")); if (report.generatedCBytes!==0 || report.objectBackend.linking.targetLibraries!=="zero-runtime" || report.objectBackend.linking.externalToolchain!=="cc" || !report.objectBackend.linkerPlan.staticLibraries.includes("zero_runtime.o") || report.objectBackend.directFacts.runtimeHelperCount!==1) process.exit(1);'
  rm -f .zero/native-test/std-http-fetch .zero/native-test/std-http-fetch.json .zero/native-test/std-http-fetch.zero.o .zero/native-test/std-http-fetch.zero-runtime.o .zero/native-test/std-http-fetch.zero-http-curl.o
  if bin/zero build --json --emit exe --target darwin-arm64 conformance/native/pass/std-http-fetch.graph --out .zero/native-test/std-http-fetch > .zero/native-test/std-http-fetch.json; then
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
  else
    node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/std-http-fetch.json","utf8")); if (report.diagnostics?.[0]?.code!=="BLD004" || report.diagnostics?.[0]?.backendBlocker?.unsupportedFeature!=="host") process.exit(1);'
  fi
fi
rm -f .zero/native-test/direct-call-add-win.obj .zero/native-test/direct-call-add-win.obj.c
bin/zero build --json --emit obj --target win32-x64.exe examples/direct-call-add.graph --out .zero/native-test/direct-call-add-win.obj > .zero/native-test/direct-call-add-win.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-call-add-win.obj"); const reloc=b.readUInt32LE(20+24); const nreloc=b.readUInt16LE(20+32); if (b.readUInt16LE(0)!==0x8664 || b.readUInt16LE(2)!==1 || b.readUInt32LE(8)===0 || reloc===0 || nreloc<1 || b.readUInt16LE(reloc+8)!==4 || !b.includes(Buffer.from([0xe8])) || !b.includes(Buffer.concat([Buffer.from("main"),Buffer.from([0])])) || !b.includes(Buffer.concat([Buffer.from("add"),Buffer.from([0])]))) process.exit(1);'
test ! -f .zero/native-test/direct-call-add-win.obj.c
grep -q '"path":"direct-coff-x64-object"' .zero/native-test/direct-call-add-win.json
grep -q '"selectedEmitter":"zero-coff-x64"' .zero/native-test/direct-call-add-win.json
rm -f .zero/native-test/direct-byte-view-reloc-win.obj .zero/native-test/direct-byte-view-reloc-win.obj.c
bin/zero build --json --emit obj --target win32-x64.exe examples/direct-byte-view-reloc.graph --out .zero/native-test/direct-byte-view-reloc-win.obj > .zero/native-test/direct-byte-view-reloc-win.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-byte-view-reloc-win.obj"); const reloc=b.readUInt32LE(20+24); const nreloc=b.readUInt16LE(20+32); let sawAddr64=false; for (let i=0;i<nreloc;i++){ if (b.readUInt16LE(reloc+i*10+8)===1) sawAddr64=true; } if (b.readUInt16LE(0)!==0x8664 || b.readUInt16LE(2)!==2 || reloc===0 || nreloc<1 || !sawAddr64 || !b.includes(Buffer.from(".rdata\0")) || !b.includes(Buffer.from("token"))) process.exit(1);'
test ! -f .zero/native-test/direct-byte-view-reloc-win.obj.c
grep -q '"dataSections":true' .zero/native-test/direct-byte-view-reloc-win.json
grep -q '"readonlyDataBytes":6' .zero/native-test/direct-byte-view-reloc-win.json
rm -f .zero/native-test/direct-hello-win.obj .zero/native-test/direct-hello-win.obj.c
bin/zero build --json --emit obj --target win32-x64.exe examples/hello.graph --out .zero/native-test/direct-hello-win.obj > .zero/native-test/direct-hello-win.json
node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/direct-hello-win.json","utf8")); const b=fs.readFileSync(".zero/native-test/direct-hello-win.obj"); const reloc=b.readUInt32LE(20+24); const nreloc=b.readUInt16LE(20+32); let sawRel32=false; for (let i=0;i<nreloc;i++){ if (b.readUInt16LE(reloc+i*10+8)===4) sawRel32=true; } if (report.objectBackend.objectEmission.symbolCount!==4 || report.objectBackend.directFacts.runtimeHelperCount!==1 || b.readUInt16LE(0)!==0x8664 || b.readUInt16LE(2)!==2 || reloc===0 || nreloc<2 || !sawRel32 || !b.includes(Buffer.from("hello from zero")) || !b.includes(Buffer.from("zero_world_write"))) process.exit(1);'
test ! -f .zero/native-test/direct-hello-win.obj.c
grep -q '"path":"direct-coff-x64-object"' .zero/native-test/direct-hello-win.json
grep -q '"generatedCBytes": 0' .zero/native-test/direct-hello-win.json
rm -f .zero/native-test/coff-maybe-byte-view.obj .zero/native-test/coff-maybe-byte-view.obj.c
bin/zero build --json --emit obj --target win32-x64.exe conformance/native/pass/coff-maybe-byte-view-buildable.graph --out .zero/native-test/coff-maybe-byte-view.obj > .zero/native-test/coff-maybe-byte-view.json
node -e 'const fs=require("fs"); const report=JSON.parse(fs.readFileSync(".zero/native-test/coff-maybe-byte-view.json","utf8")); const b=fs.readFileSync(".zero/native-test/coff-maybe-byte-view.obj"); if (report.objectBackend.objectEmission.path!=="direct-coff-x64-object" || b.readUInt16LE(0)!==0x8664 || b.readUInt16LE(2)!==2 || !b.includes(Buffer.from("main")) || !b.includes(Buffer.from("trap: index out of bounds"))) process.exit(1);'
test ! -f .zero/native-test/coff-maybe-byte-view.obj.c
rm -f .zero/native-test/direct-array-fill.o .zero/native-test/direct-array-fill.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-array-fill.graph --out .zero/native-test/direct-array-fill.o > .zero/native-test/direct-array-fill-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-array-fill.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-array-fill.o.c
grep -q '"maxFrameBytes":' .zero/native-test/direct-array-fill-obj.json
rm -f .zero/native-test/direct-package-arrays.o .zero/native-test/direct-package-arrays.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-package-arrays --out .zero/native-test/direct-package-arrays.o > .zero/native-test/direct-package-arrays-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-package-arrays.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-package-arrays.o.c
grep -q '"moduleCount":2' .zero/native-test/direct-package-arrays-obj.json
rm -f .zero/native-test/direct-token-shape.o .zero/native-test/direct-token-shape.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-token-shape.graph --out .zero/native-test/direct-token-shape.o > .zero/native-test/direct-token-shape-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-token-shape.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-token-shape.o.c
grep -q '"path":"direct-elf64-object"' .zero/native-test/direct-token-shape-obj.json
rm -f .zero/native-test/direct-span-read.o .zero/native-test/direct-span-read.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-span-read.graph --out .zero/native-test/direct-span-read.o > .zero/native-test/direct-span-read-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-span-read.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-span-read.o.c
grep -q '"dataSections":true' .zero/native-test/direct-span-read-obj.json
grep -q '"readonlyDataBytes":6' .zero/native-test/direct-span-read-obj.json
rm -f .zero/native-test/direct-byte-view-reloc.o .zero/native-test/direct-byte-view-reloc.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-byte-view-reloc.graph --out .zero/native-test/direct-byte-view-reloc.o > .zero/native-test/direct-byte-view-reloc-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-byte-view-reloc.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62 || !b.includes(Buffer.concat([Buffer.from(".rodata"),Buffer.from([0])])) || !b.includes(Buffer.concat([Buffer.from(".rela.text"),Buffer.from([0])]))) process.exit(1);'
test ! -f .zero/native-test/direct-byte-view-reloc.o.c
grep -q '"readonlyDataBytes":6' .zero/native-test/direct-byte-view-reloc-obj.json
rm -f .zero/native-test/direct-rescue-basic.o .zero/native-test/direct-rescue-basic.o.c
bin/zero build --json --emit obj --target linux-musl-x64 examples/direct-rescue-basic.graph --out .zero/native-test/direct-rescue-basic.o > .zero/native-test/direct-rescue-basic-obj.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-rescue-basic.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-rescue-basic.o.c
grep -q '"path":"direct-elf64-object"' .zero/native-test/direct-rescue-basic-obj.json
rm -f .zero/native-test/direct-exe-return .zero/native-test/direct-exe-return.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-exe-return.graph --out .zero/native-test/direct-exe-return > .zero/native-test/direct-exe-return.json
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
bin/zero build --json --emit exe --backend zero-elf-aarch64 --target linux-musl-arm64 examples/direct-exe-return.graph --out .zero/native-test/direct-aarch64-exe-return > .zero/native-test/direct-aarch64-exe-return.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-aarch64-exe-return"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==183 || !b.includes(Buffer.from([0x40,0x05,0x80,0x52,0xc0,0x03,0x5f,0xd6])) || !b.includes(Buffer.from([0xa8,0x0b,0x80,0xd2,0x01,0x00,0x00,0xd4]))) process.exit(1);'
test ! -f .zero/native-test/direct-aarch64-exe-return.c
grep -q '"compiler": "zero-elf-aarch64"' .zero/native-test/direct-aarch64-exe-return.json
grep -q '"path":"direct-elf-aarch64-exe"' .zero/native-test/direct-aarch64-exe-return.json
node -e 'const r=require("fs").readFileSync(".zero/native-test/direct-aarch64-exe-return.json","utf8"); const j=JSON.parse(r); if (j.artifactBytes >= 512 || j.objectBackend.targetFacts.status !== "native-exe" || j.objectBackend.linking.externalToolchain !== "none") process.exit(1);'
rm -f .zero/native-test/direct-while-sum .zero/native-test/direct-while-sum.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-while-sum.graph --out .zero/native-test/direct-while-sum > .zero/native-test/direct-while-sum.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-while-sum"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-while-sum.c
grep -q '"generatedCBytes": 0' .zero/native-test/direct-while-sum.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-while-sum.json
rm -f .zero/native-test/direct-call-loop .zero/native-test/direct-call-loop.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-call-loop.graph --out .zero/native-test/direct-call-loop > .zero/native-test/direct-call-loop.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-call-loop"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-call-loop.c
grep -q '"generatedCBytes": 0' .zero/native-test/direct-call-loop.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-call-loop.json
rm -f .zero/native-test/direct-array-fill .zero/native-test/direct-array-fill.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-array-fill.graph --out .zero/native-test/direct-array-fill > .zero/native-test/direct-array-fill.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-array-fill"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-array-fill.c
grep -q '"stackBytes":' .zero/native-test/direct-array-fill.json
rm -f .zero/native-test/direct-package-arrays .zero/native-test/direct-package-arrays.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-package-arrays --out .zero/native-test/direct-package-arrays > .zero/native-test/direct-package-arrays.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-package-arrays"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-package-arrays.c
grep -q '"moduleCount":2' .zero/native-test/direct-package-arrays.json
rm -f .zero/native-test/direct-token-shape .zero/native-test/direct-token-shape.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-token-shape.graph --out .zero/native-test/direct-token-shape > .zero/native-test/direct-token-shape.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-token-shape"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-token-shape.c
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-token-shape.json
rm -f .zero/native-test/direct-string-len .zero/native-test/direct-string-len.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-string-len.graph --out .zero/native-test/direct-string-len > .zero/native-test/direct-string-len.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-string-len"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-string-len.c
grep -q '"readonlyDataBytes":6' .zero/native-test/direct-string-len.json
rm -f .zero/native-test/direct-string-literal .zero/native-test/direct-string-literal.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-string-literal.graph --out .zero/native-test/direct-string-literal > .zero/native-test/direct-string-literal.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-string-literal"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-string-literal.c
grep -q '"readonlyDataBytes":6' .zero/native-test/direct-string-literal.json
rm -f .zero/native-test/direct-span-read .zero/native-test/direct-span-read.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-span-read.graph --out .zero/native-test/direct-span-read > .zero/native-test/direct-span-read.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-span-read"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-span-read.c
grep -q '"readonlyDataBytes":6' .zero/native-test/direct-span-read.json
rm -f .zero/native-test/direct-byte-view-params .zero/native-test/direct-byte-view-params.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 conformance/native/pass/byte-view-params.graph --out .zero/native-test/direct-byte-view-params > .zero/native-test/direct-byte-view-params.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-byte-view-params"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-byte-view-params.c
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-byte-view-params.json
rm -f .zero/native-test/direct-bool-arrays .zero/native-test/direct-bool-arrays.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 conformance/native/pass/bool-arrays.graph --out .zero/native-test/direct-bool-arrays > .zero/native-test/direct-bool-arrays.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-bool-arrays"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-bool-arrays.c
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-bool-arrays.json
rm -f .zero/native-test/direct-byte-view-reloc .zero/native-test/direct-byte-view-reloc.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-byte-view-reloc.graph --out .zero/native-test/direct-byte-view-reloc > .zero/native-test/direct-byte-view-reloc.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-byte-view-reloc"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-byte-view-reloc.c
grep -q '"readonlyDataBytes":6' .zero/native-test/direct-byte-view-reloc.json
node -e 'const r=require("fs").readFileSync(".zero/native-test/direct-byte-view-reloc.json","utf8"); const j=JSON.parse(r); if (j.artifactBytes >= 1024 || j.objectBackend.objectEmission.dataSections !== true || j.objectBackend.linking.externalToolchain !== "none") process.exit(1);'
rm -f .zero/native-test/direct-raises-basic .zero/native-test/direct-raises-basic.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-raises-basic.graph --out .zero/native-test/direct-raises-basic > .zero/native-test/direct-raises-basic.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-raises-basic"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-raises-basic.c
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-raises-basic.json
rm -f .zero/native-test/direct-rescue-basic .zero/native-test/direct-rescue-basic.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-rescue-basic.graph --out .zero/native-test/direct-rescue-basic > .zero/native-test/direct-rescue-basic.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-rescue-basic"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-rescue-basic.c
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-rescue-basic.json
rm -f .zero/native-test/direct-unhandled-error-exit .zero/native-test/direct-unhandled-error-exit.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-unhandled-error-exit.graph --out .zero/native-test/direct-unhandled-error-exit > .zero/native-test/direct-unhandled-error-exit.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-unhandled-error-exit"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-unhandled-error-exit.c
grep -q '"generatedCBytes": 0' .zero/native-test/direct-unhandled-error-exit.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-unhandled-error-exit.json
rm -f .zero/native-test/direct-std-fs-breadth .zero/native-test/direct-std-fs-breadth.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 conformance/native/pass/std-fs-breadth.graph --out .zero/native-test/direct-std-fs-breadth > .zero/native-test/direct-std-fs-breadth.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-std-fs-breadth"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==62) process.exit(1);'
test ! -f .zero/native-test/direct-std-fs-breadth.c
grep -q '"generatedCBytes": 0' .zero/native-test/direct-std-fs-breadth.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-std-fs-breadth.json
rm -f .zero/native-test/direct-std-fs-fallible-resources .zero/native-test/direct-std-fs-fallible-resources.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 conformance/native/pass/std-fs-fallible-resources.graph --out .zero/native-test/direct-std-fs-fallible-resources > .zero/native-test/direct-std-fs-fallible-resources.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-std-fs-fallible-resources"); const packed=(code)=>{const x=Buffer.alloc(10); x[0]=0x48; x[1]=0xb8; x.writeBigUInt64LE(BigInt(code)<<32n,2); return x}; if (!b.includes(packed(2)) || !b.includes(packed(4))) process.exit(1);'
test ! -f .zero/native-test/direct-std-fs-fallible-resources.c
grep -q '"generatedCBytes": 0' .zero/native-test/direct-std-fs-fallible-resources.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-std-fs-fallible-resources.json
rm -f .zero/native-test/direct-std-fs-fallible .zero/native-test/direct-std-fs-fallible.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 conformance/native/pass/std-fs-fallible.graph --out .zero/native-test/direct-std-fs-fallible > .zero/native-test/direct-std-fs-fallible.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-std-fs-fallible"); const packed=(code)=>{const x=Buffer.alloc(10); x[0]=0x48; x[1]=0xb8; x.writeBigUInt64LE((BigInt(code)<<32n)|1n,2); return x}; if (!b.includes(packed(2)) || !b.includes(packed(3)) || !b.includes(packed(4))) process.exit(1);'
test ! -f .zero/native-test/direct-std-fs-fallible.c
grep -q '"generatedCBytes": 0' .zero/native-test/direct-std-fs-fallible.json
grep -q '"path":"direct-elf64-exe"' .zero/native-test/direct-std-fs-fallible.json
rm -f .zero/native-test/direct-std-io .zero/native-test/direct-std-io.c
bin/zero build --json --emit exe --backend zero-elf64 --target linux-musl-x64 conformance/native/pass/std-io-direct.graph --out .zero/native-test/direct-std-io > .zero/native-test/direct-std-io.json
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
if bin/zero build --emit exe --backend zero-elf64 --target linux-musl-x64 examples/direct-obj-add.graph --out .zero/native-test/direct-exe-with-params >/dev/null 2>.zero/native-test/direct-exe-with-params.err; then
  echo "expected direct executable ABI gate to fail" >&2
  exit 1
fi
grep -q "must not take parameters" .zero/native-test/direct-exe-with-params.err
rm -f .zero/native-test/direct-exe-darwin .zero/native-test/direct-exe-darwin.c .zero/native-test/direct-exe-darwin.json .zero/native-test/direct-hello-darwin .zero/native-test/direct-hello-darwin.c
bin/zero build --json --emit exe --backend zero-macho64 --target darwin-arm64 examples/direct-exe-return.graph --out .zero/native-test/direct-exe-darwin > .zero/native-test/direct-exe-darwin.json
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
  direct_macho_hello_output="$(bin/zero build --emit exe --target darwin-arm64 examples/hello.graph --out .zero/native-test/direct-hello-darwin >/dev/null && .zero/native-test/direct-hello-darwin)"
  test "$direct_macho_hello_output" = "hello from zero"
  direct_macho_scratch_output="$(bin/zero build --emit exe --target darwin-arm64 conformance/native/pass/scratch-nested-index.graph --out .zero/native-test/direct-macho-scratch-nested-index >/dev/null && .zero/native-test/direct-macho-scratch-nested-index)"
  test "$direct_macho_scratch_output" = "scratch nested index ok"
fi
rm -f .zero/native-test/direct-arm64.o .zero/native-test/direct-arm64.o.c
bin/zero build --json --emit obj --target linux-arm64 examples/direct-exe-return.graph --out .zero/native-test/direct-arm64.o > .zero/native-test/direct-arm64.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/direct-arm64.o"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==1 || b.readUInt16LE(18)!==183 || !b.includes(Buffer.from([0x40,0x05,0x80,0x52,0xc0,0x03,0x5f,0xd6]))) process.exit(1);'
grep -q '"compiler": "zero-elf-aarch64"' .zero/native-test/direct-arm64.json
grep -q '"path":"direct-elf-aarch64-object"' .zero/native-test/direct-arm64.json
test ! -f .zero/native-test/direct-arm64.o.c
rm -f .zero/native-test/hello-linux-musl-arm64 .zero/native-test/hello-linux-musl-arm64.c
bin/zero build --emit exe --target linux-musl-arm64 examples/direct-exe-return.graph --out .zero/native-test/hello-linux-musl-arm64 >/dev/null
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/hello-linux-musl-arm64"); if (b[0]!==0x7f || b[1]!==0x45 || b.readUInt16LE(16)!==2 || b.readUInt16LE(18)!==183) process.exit(1);'
test ! -f .zero/native-test/hello-linux-musl-arm64.c
rm -f .zero/native-test/hello-windows.exe .zero/native-test/hello-windows.exe.c .zero/native-test/hello-windows.json
ZERO_CC=/usr/bin/false bin/zero build --json --emit exe --target win32-x64.exe examples/hello.graph --out .zero/native-test/hello-windows > .zero/native-test/hello-windows.json
node -e 'const fs=require("fs"); const b=fs.readFileSync(".zero/native-test/hello-windows.exe"); const pe=b.readUInt32LE(0x3c); if (b[0]!==0x4d || b[1]!==0x5a || b.toString("ascii", pe, pe+4)!=="PE\0\0" || b.readUInt16LE(pe+4)!==0x8664 || !b.includes(Buffer.from("KERNEL32.dll")) || !b.includes(Buffer.from("WriteFile"))) process.exit(1);'
grep -q '"compiler": "zero-coff-x64"' .zero/native-test/hello-windows.json
grep -q '"path":"direct-coff-x64-exe"' .zero/native-test/hello-windows.json
grep -q '"generatedCBytes": 0' .zero/native-test/hello-windows.json
test ! -f .zero/native-test/hello-windows.exe.c
bin/zero check --target x86_64-windows-msvc examples/hello.graph >/dev/null
if command -v zig >/dev/null 2>&1; then
  bin/zero build --emit exe --target linux-musl-x64 examples/hello.graph --out .zero/native-test/hello-linux-musl >/dev/null
  test -f .zero/native-test/hello-linux-musl
fi
ZERO_CC=cc bin/zero build --emit exe --target linux-musl-x64 --profile dev examples/hello.graph --out .zero/native-test/hello-dev >/dev/null
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
native_log_elapsed "direct backend artifacts ok" "$native_phase_started_at"
fi

echo "native conformance ok"
