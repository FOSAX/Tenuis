#!/usr/bin/env bash
# Phase 3 compression test suite.
# Tests: TCF round-trip, compressed .tenb execution, regression on previous tests.
# Usage: ./tests/run_phase3_tests.sh <build-dir>
set -euo pipefail
BUILD=${1:-build}
TENUISC="${BUILD}/tenuisc"
VM="${BUILD}/tenuisr"
ROUNDTRIP="${BUILD}/tcf_roundtrip"
FIXTURES="$(dirname "$0")/fixtures"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

PASS=0; FAIL=0

pass() { printf "  PASS  %s\n" "$1"; PASS=$((PASS+1)); }
fail() { printf "  FAIL  %s\n    %s\n" "$1" "$2"; FAIL=$((FAIL+1)); }

echo "=== Phase 3 compression tests ==="

# ── 1. TCF round-trip: small binary (should expand, but still round-trip) ──
printf '\x02\x2a\x50\x01' > "${TMP}/tiny.bin"
if "$ROUNDTRIP" "${TMP}/tiny.bin" 2>/dev/null; then
    pass "tcf_roundtrip_tiny"
else
    fail "tcf_roundtrip_tiny" "round-trip failed for 4-byte input"
fi

# ── 2. TCF round-trip: repeated-pattern binary (should compress) ──────────
# Build a 400-byte file: 4-byte pattern repeated 100 times
python3 -c "import sys; sys.stdout.buffer.write(bytes([0x02, 0x00, 0x50, 0x01] * 100))" \
    > "${TMP}/pattern.bin" 2>/dev/null || {
    # Fallback without python3: use printf
    for _ in $(seq 1 100); do printf '\x02\x00\x50\x01'; done > "${TMP}/pattern.bin"
}
if "$ROUNDTRIP" "${TMP}/pattern.bin" 2>/dev/null; then
    pass "tcf_roundtrip_repeated_pattern"
else
    fail "tcf_roundtrip_repeated_pattern" "round-trip failed for repeated-pattern input"
fi

# ── 3. Triangles fixture: regression test ─────────────────────────────────
tenb="${TMP}/triangles.tenb"
if ! "$TENUISC" "${FIXTURES}/triangles.ten" "$tenb" 2>/dev/null; then
    fail "triangles_compile" "compiler error"
else
    actual=$("$VM" "$tenb" 2>/dev/null || true)
    expected=$(printf '\x01\x03\x06\x0a\x0f\x15\x1c\x24\x2d\x37')
    if [ "$actual" = "$expected" ]; then
        pass "triangles"
    else
        fail "triangles" \
            "got $(printf '%s' "$actual" | xxd -p), expected $(printf '%s' "$expected" | xxd -p)"
    fi
fi

# ── 4. Large program: triggers F_COMPRESSED=1 ──────────────────────────────
# Generate a 256-pair emit program: emit bytes 0..127 twice.
# Bytecode: 128 × (PUSH8 n, EMIT) × 2 repetitions ≈ 768 bytes → should compress.
{
    for pass_num in 1 2; do
        for i in $(seq 0 127); do printf '%d . ' "$i"; done
    done
    echo '_'
} > "${TMP}/large.ten"

large_tenb="${TMP}/large.tenb"
if ! "$TENUISC" "${TMP}/large.ten" "$large_tenb" 2>/dev/null; then
    fail "large_compile" "compiler error on large.ten"
else
    # Check F_COMPRESSED flag (byte 5, bit 0 of .tenb)
    flags_byte=$(od -An -tx1 -j5 -N1 "$large_tenb" | tr -d ' ')
    compressed_flag=$(( 0x${flags_byte} & 1 ))
    if [ "$compressed_flag" -eq 1 ]; then
        pass "large_compressed_flag"
    else
        fail "large_compressed_flag" \
            "expected F_COMPRESSED=1 (flags=0x${flags_byte}); bytecode may be too small to benefit"
    fi

    # Run and verify output: 256 bytes = 0x00..0x7F twice
    actual_hex=$("$VM" "$large_tenb" 2>/dev/null | od -An -tx1 | tr -d ' \n' || true)
    expected_hex=""
    for rep in 1 2; do
        for i in $(seq 0 127); do
            expected_hex="${expected_hex}$(printf '%02x' "$i")"
        done
    done
    if [ "$actual_hex" = "$expected_hex" ]; then
        pass "large_output"
    else
        fail "large_output" "output mismatch (first 8 bytes got: ${actual_hex:0:16})"
    fi
fi

# ── 5. Regression: all phase-2 test programs still run correctly ───────────
compile_run_check() {
    local name="$1" src_file="$2" expected="$3"
    local tenb_local="${TMP}/${name}.tenb"
    if ! "$TENUISC" "$src_file" "$tenb_local" 2>/dev/null; then
        fail "${name}_regression" "compiler error"; return
    fi
    local actual; actual=$("$VM" "$tenb_local" 2>/dev/null || true)
    if [ "$actual" = "$expected" ]; then pass "${name}_regression"
    else
        fail "${name}_regression" \
            "got $(printf '%s' "$actual" | xxd -p), expected $(printf '%s' "$expected" | xxd -p)"
    fi
}
compile_run_check "arith"  "${FIXTURES}/arith.ten"  "$(printf '\x0d')"
compile_run_check "branch" "${FIXTURES}/branch.ten" "Y"
compile_run_check "loop"   "${FIXTURES}/loop.ten"   "$(printf 'ABCDE')"
compile_run_check "sumto"  "${FIXTURES}/sumto.ten"  "$(printf '\x0f')"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
