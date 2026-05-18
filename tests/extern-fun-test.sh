#!/bin/bash
set -e

# Extern function call integration tests
# These tests verify that Zero can call external C functions through proper linking

ZERO=./bin/zero
TMPDIR=.zero/tmp/extern-tests
mkdir -p "$TMPDIR"

echo "=== Extern Function Call Tests ==="
echo

# Test 1: libc abs() function
echo "Test 1: libc abs() function"
$ZERO build --emit obj conformance/native/pass/extern-fun-libc-abs.0 --out "$TMPDIR/test1.o"
cc "$TMPDIR/test1.o" -o "$TMPDIR/test1"
output=$("$TMPDIR/test1")
if [ "$output" = "extern call ok" ]; then
    echo "  ✓ PASS"
else
    echo "  ✗ FAIL: expected 'extern call ok', got '$output'"
    exit 1
fi
echo

# Test 2: Custom C functions
echo "Test 2: Custom C functions"
cc -c conformance/c/extern-helpers.c -o "$TMPDIR/helpers.o"
$ZERO build --emit obj conformance/native/pass/extern-fun-custom.0 --out "$TMPDIR/test2.o"
cc "$TMPDIR/test2.o" "$TMPDIR/helpers.o" -o "$TMPDIR/test2"
output=$("$TMPDIR/test2")
if [ "$output" = "custom extern ok" ]; then
    echo "  ✓ PASS"
else
    echo "  ✗ FAIL: expected 'custom extern ok', got '$output'"
    exit 1
fi
echo

# Test 3: Six arguments (register + stack passing)
echo "Test 3: Six arguments (System V AMD64 ABI)"
$ZERO build --emit obj conformance/native/pass/extern-fun-six-args.0 --out "$TMPDIR/test3.o"
cc "$TMPDIR/test3.o" "$TMPDIR/helpers.o" -o "$TMPDIR/test3"
output=$("$TMPDIR/test3")
if [ "$output" = "six args ok" ]; then
    echo "  ✓ PASS"
else
    echo "  ✗ FAIL: expected 'six args ok', got '$output'"
    exit 1
fi
echo

# Test 4: Void return type
echo "Test 4: Void return type"
$ZERO build --emit obj conformance/native/pass/extern-fun-void-return.0 --out "$TMPDIR/test4.o"
cc "$TMPDIR/test4.o" "$TMPDIR/helpers.o" -o "$TMPDIR/test4"
output=$("$TMPDIR/test4")
if [ "$output" = "void extern ok" ]; then
    echo "  ✓ PASS"
else
    echo "  ✗ FAIL: expected 'void extern ok', got '$output'"
    exit 1
fi
echo

echo "=== All extern function tests passed ==="
