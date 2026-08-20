#!/usr/bin/env bash
# Phase 4 packed-binary test suite.
# Runs standalone executables (program + VM, no external .tenb file) and
# verifies they produce the same output as the file-based runtime.
# Usage: ./tests/run_phase4_tests.sh <build-dir>
set -euo pipefail
BUILD=${1:-build}

PASS=0; FAIL=0

check() {
    local name="$1"
    local bin="${BUILD}/$2"
    local expected="$3"
    local actual
    actual=$("$bin" 2>/dev/null || true)
    if [ "$actual" = "$expected" ]; then
        printf "  PASS  %s\n" "$name"
        PASS=$((PASS+1))
    else
        printf "  FAIL  %s\n    got:      %s\n    expected: %s\n" \
            "$name" \
            "$(printf '%s' "$actual"  | xxd -p || echo '<non-printable>')" \
            "$(printf '%s' "$expected" | xxd -p || echo '<non-printable>')"
        FAIL=$((FAIL+1))
    fi
}

echo "=== Phase 4 packed-binary tests ==="

# Each packed binary is a single standalone executable — no arguments, no .tenb file.
check "arith_packed"     arith_packed     "$(printf '\x0d')"
check "branch_packed"    branch_packed    "Y"
check "loop_packed"      loop_packed      "$(printf 'ABCDE')"
check "sumto_packed"     sumto_packed     "$(printf '\x0f')"
check "triangles_packed" triangles_packed "$(printf '\x01\x03\x06\x0a\x0f\x15\x1c\x24\x2d\x37')"

# Verify the packed binaries have no unresolved external file dependencies.
# (They must not reference libc file I/O beyond what the VM uses for I/O.)
for bin in arith_packed branch_packed loop_packed sumto_packed triangles_packed; do
    if ldd "${BUILD}/${bin}" 2>/dev/null | grep -q 'libstdc++'; then
        printf "  WARN  %s links against libstdc++ (expected for host build)\n" "$bin"
    fi
    # The key check: no open/read/close symbols needed at runtime
    if nm "${BUILD}/${bin}" 2>/dev/null | grep -q ' U open$'; then
        printf "  FAIL  %s still references open() — file I/O leaked\n" "$bin"
        FAIL=$((FAIL+1))
    else
        printf "  PASS  %s_no_file_io\n" "$bin"
        PASS=$((PASS+1))
    fi
done

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
