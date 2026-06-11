#!/bin/sh
# CLove FH test runner.
#
# Each tests/fh/test_*.fh is a self-contained CLove program: its love_load()
# runs assertions (failing via FH's error()), prints CLOVE_TEST_OK and quits.
# Each tests/fh/xfail_*.fh is expected to fail (exit non-zero) — these lock in
# the argument-validation guards in the bindings.
#
# CLove always loads "main.fh" from the current directory, so every test is
# copied into a scratch dir as main.fh and run from there. Pass/fail is taken
# from the process exit code (0 = ok) and, for positive tests, the presence of
# the CLOVE_TEST_OK marker.
#
# Usage: tests/run_tests.sh [path-to-clove-binary]
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)

clove=${1:-}
if [ -z "$clove" ]; then
    for c in "$root/build/clove" "$root/build/install/usr/local/bin/clove" "$root/clove"; do
        [ -x "$c" ] && clove="$c" && break
    done
fi
if [ -z "$clove" ] || [ ! -x "$clove" ]; then
    echo "error: clove binary not found. Build it first (./build_osx.sh or"
    echo "       ./build_linux.sh) or pass its path: tests/run_tests.sh path/to/clove"
    exit 2
fi

# A real window briefly appears per test unless a headless video driver works.
# SDL's dummy driver has no GL, so only force it when explicitly requested.
[ "${CLOVE_HEADLESS:-0}" = "1" ] && export SDL_VIDEODRIVER=dummy

scratch=$(mktemp -d "${TMPDIR:-/tmp}/clove_tests.XXXXXX")
trap 'rm -rf "$scratch"' EXIT

pass=0
fail=0
failed_list=""

run_one() {
    test_file=$1
    name=$(basename "$test_file" .fh)
    expect_fail=0
    case "$name" in xfail_*) expect_fail=1 ;; esac

    rm -f "$scratch/main.fh"
    cp "$test_file" "$scratch/main.fh"

    out=$( cd "$scratch" && "$clove" 2>&1 )
    code=$?

    ok=0
    if [ "$expect_fail" -eq 1 ]; then
        # must fail and must not have reached the success marker
        if [ "$code" -ne 0 ] && ! printf '%s' "$out" | grep -q CLOVE_TEST_OK; then
            ok=1
        fi
    else
        if [ "$code" -eq 0 ] && printf '%s' "$out" | grep -q CLOVE_TEST_OK; then
            ok=1
        fi
    fi

    if [ "$ok" -eq 1 ]; then
        pass=$((pass + 1))
        printf '  PASS  %s\n' "$name"
    else
        fail=$((fail + 1))
        failed_list="$failed_list $name"
        printf '  FAIL  %s (exit %d)\n' "$name" "$code"
        printf '%s\n' "$out" | grep -iE 'error|expected|should' | sed 's/^/        /'
    fi
}

echo "Running CLove FH tests with: $clove"
echo

for t in "$here"/fh/test_*.fh; do
    [ -e "$t" ] && run_one "$t"
done
for t in "$here"/fh/xfail_*.fh; do
    [ -e "$t" ] && run_one "$t"
done

echo
echo "------------------------------------"
echo "passed: $pass   failed: $fail"
if [ "$fail" -ne 0 ]; then
    echo "failed:$failed_list"
    exit 1
fi
echo "all tests passed"
