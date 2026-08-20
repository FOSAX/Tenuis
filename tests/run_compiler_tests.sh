#!/usr/bin/env bash
# Phase 2 compiler test suite.
# Compiles each .ten fixture with tenuisc, runs it with tenuisr, checks output.
# Usage: ./tests/run_compiler_tests.sh <build-dir>
set -euo pipefail
BUILD=${1:-build}
TENUISC="${BUILD}/tenuisc"
VM="${BUILD}/tenuisr"
FIXTURES="$(dirname "$0")/fixtures"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

PASS=0; FAIL=0

compile_run_check() {
    local name="$1"
    local src_file="$2"
    local expected="$3"
    local tenb="${TMP}/${name}.tenb"

    # Compile
    if ! "$TENUISC" "$src_file" "$tenb" 2>/dev/null; then
        printf "  FAIL  %s  (compiler error)\n" "$name"
        FAIL=$((FAIL+1))
        return
    fi

    # Run
    local actual
    actual=$("$VM" "$tenb" 2>/dev/null || true)

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

echo "=== Phase 2 compiler tests ==="

# ── 1. arith.ten: (3+4)*2-1 = 13 ─────────────────────────────────────────
compile_run_check "arith" "${FIXTURES}/arith.ten" "$(printf '\x0d')"

# ── 2. branch.ten: 5 > 3 → emit 'Y' ─────────────────────────────────────
compile_run_check "branch" "${FIXTURES}/branch.ten" "Y"

# ── 3. loop.ten: emit 'A'..'E' ────────────────────────────────────────────
compile_run_check "loop" "${FIXTURES}/loop.ten" "$(printf 'ABCDE')"

# ── 4. sumto.ten: sum_to(5) = 15 ──────────────────────────────────────────
compile_run_check "sumto" "${FIXTURES}/sumto.ten" "$(printf '\x0f')"

# ── 5. Negative literal ────────────────────────────────────────────────────
cat > "${TMP}/neg.ten" << 'EOF'
// push -5, negate → 5, emit
-5 15 . _
EOF
# Wait: `-5 15 .` → PUSH8 -5, then `15 .` → PUSH8 15, EMIT(15), but we want -5 then negate
cat > "${TMP}/neg.ten" << 'EOF'
-5 ~ 1 + .
_
EOF
# -5 bitnot = 4, 4+1 = 5
compile_run_check "neg_literal" "${TMP}/neg.ten" "$(printf '\x05')"

# ── 6. PUSH16: value 300 (> int8 range), emit low byte = 44 ──────────────
cat > "${TMP}/push16.ten" << 'EOF'
300 .
_
EOF
compile_run_check "push16_300_low_byte_44" "${TMP}/push16.ten" "$(printf '\x2c')"

# ── 7. PUSH32: value 65536, emit low byte = 0 ─────────────────────────────
cat > "${TMP}/push32.ten" << 'EOF'
65536 .
_
EOF
compile_run_check "push32_65536_low_byte_0" "${TMP}/push32.ten" "$(printf '\x00')"

# ── 8. Nested call: double-then-increment ─────────────────────────────────
cat > "${TMP}/nested.ten" << 'EOF'
// double(4) = 8, then inc(8) = 9, emit 9
4 (dbl) (inc) .
_

:dbl   $ + ;       // ( n -- 2*n )  via DUP + ADD
:inc   1 + ;       // ( n -- n+1 )
EOF
compile_run_check "nested_call" "${TMP}/nested.ten" "$(printf '\x09')"

# ── 9. Memory round-trip via compiler ─────────────────────────────────────
cat > "${TMP}/mem.ten" << 'EOF'
// store 42 at addr 8, load it back, emit
42 8 !    // STORE32: ( val addr -- )
8 @       // LOAD32:  ( addr -- val )
.
_
EOF
compile_run_check "mem_store_load_42" "${TMP}/mem.ten" "$(printf '\x2a')"

# ── 10. Bytecode size check: arith.ten should produce exactly 13 bytes ────
"$TENUISC" "${FIXTURES}/arith.ten" "${TMP}/arith_sz.tenb" 2>/dev/null
TENB_SIZE=$(wc -c < "${TMP}/arith_sz.tenb")
CODE_SIZE=$((TENB_SIZE - 20))
if [ "$CODE_SIZE" -eq 13 ]; then
    printf "  PASS  arith_code_size_13_bytes\n"
    PASS=$((PASS+1))
else
    printf "  FAIL  arith_code_size (got %d bytes, expected 13)\n" "$CODE_SIZE"
    FAIL=$((FAIL+1))
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
