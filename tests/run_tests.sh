#!/usr/bin/env bash
# Phase 1 test suite — exercises every major opcode group.
# Usage: ./tests/run_tests.sh <build-dir>
set -euo pipefail
BUILD=${1:-build}
MKTENB="${BUILD}/mktenb"
VM="${BUILD}/tenuisr"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

PASS=0; FAIL=0

check() {
    local name="$1"; local tenb="$2"; local expected="$3"
    local actual
    actual=$("$VM" "$tenb" 2>/dev/null || true)
    if [ "$actual" = "$expected" ]; then
        printf "  PASS  %s\n" "$name"
        PASS=$((PASS+1))
    else
        printf "  FAIL  %s\n    got:      $(printf '%s' "$actual" | xxd -p)\n    expected: $(printf '%s' "$expected" | xxd -p)\n" "$name"
        FAIL=$((FAIL+1))
    fi
}

echo "=== Phase 1 VM tests ==="

# ── 1. PUSH8 + EMIT + HALT ────────────────────────────────────────────────
# push 65 ('A'), emit, halt
# 02 41  50  01
"$MKTENB" "$TMP/t1.tenb" 02 41 50 01
check "emit_A" "$TMP/t1.tenb" "A"

# ── 2. ADD ────────────────────────────────────────────────────────────────
# push 10, push 3, add (→13), emit, halt
# 02 0A  02 03  10  50  01
"$MKTENB" "$TMP/t2.tenb" 02 0A 02 03 10 50 01
check "add_10+3=13" "$TMP/t2.tenb" "$(printf '\x0d')"

# ── 3. SUB ────────────────────────────────────────────────────────────────
# push 10, push 3, sub (→7), emit, halt
"$MKTENB" "$TMP/t3.tenb" 02 0A 02 03 11 50 01
check "sub_10-3=7" "$TMP/t3.tenb" "$(printf '\x07')"

# ── 4. MUL + DUP ──────────────────────────────────────────────────────────
# push 5, dup (→5 5), mul (→25), emit, halt
"$MKTENB" "$TMP/t4.tenb" 02 05 06 12 50 01
check "dup_mul_5*5=25" "$TMP/t4.tenb" "$(printf '\x19')"

# ── 5. DIV + MOD ──────────────────────────────────────────────────────────
# push 17, dup, push 5, div (→3), swap, push 5, mod (→2),
# add (→5), emit, halt
# 17=0x11, 5=0x05
# [02 11] [06] [02 05] [13] [07] [02 05] [14] [10] [50] [01]
"$MKTENB" "$TMP/t5.tenb" 02 11 06 02 05 13 07 02 05 14 10 50 01
check "div_mod_17/5=3_17%5=2_sum=5" "$TMP/t5.tenb" "$(printf '\x05')"

# ── 6. NEG + BITNOT ───────────────────────────────────────────────────────
# push 1, neg (→-1), bitnot (→0), emit, halt
"$MKTENB" "$TMP/t6.tenb" 02 01 15 1B 50 01
check "neg_bitnot_1_neg_bitnot=0" "$TMP/t6.tenb" "$(printf '\x00')"

# ── 7. Bitwise AND/OR/XOR ─────────────────────────────────────────────────
# 0xFF & 0x0F = 0x0F, emit
"$MKTENB" "$TMP/t7a.tenb" 02 FF 02 0F 18 50 01   # hmm, 0xFF doesn't fit in int8 range cleanly
# 0xFF as int8 = -1 (sign-extended to -1), 0x0F = 15, -1 & 15 = 15 = 0x0F
check "and_ff_0f=0f" "$TMP/t7a.tenb" "$(printf '\x0f')"
# 0xF0 | 0x0F = 0xFF, emit low byte = 0xFF
"$MKTENB" "$TMP/t7b.tenb" 02 F0 02 0F 19 50 01
# 0xF0 as int8 = -16, 0x0F = 15, -16 | 15 = -1, emit low byte = 0xFF
check "or_f0_0f=ff" "$TMP/t7b.tenb" "$(printf '\xff')"

# ── 8. SHL / SHR ──────────────────────────────────────────────────────────
# 1 << 3 = 8, emit
"$MKTENB" "$TMP/t8a.tenb" 02 01 02 03 1C 50 01
check "shl_1<<3=8" "$TMP/t8a.tenb" "$(printf '\x08')"
# 64 >> 2 = 16
"$MKTENB" "$TMP/t8b.tenb" 02 40 02 02 1D 50 01
check "shr_64>>2=16" "$TMP/t8b.tenb" "$(printf '\x10')"

# ── 9. Comparison EQ / LT / GT ────────────────────────────────────────────
# 5 == 5 → 1, emit
"$MKTENB" "$TMP/t9a.tenb" 02 05 02 05 20 50 01
check "eq_5==5=1" "$TMP/t9a.tenb" "$(printf '\x01')"
# 3 < 7 → 1
"$MKTENB" "$TMP/t9b.tenb" 02 03 02 07 22 50 01
check "lt_3<7=1" "$TMP/t9b.tenb" "$(printf '\x01')"
# 7 > 3 → 1
"$MKTENB" "$TMP/t9c.tenb" 02 07 02 03 23 50 01
check "gt_7>3=1" "$TMP/t9c.tenb" "$(printf '\x01')"

# ── 10. SWAP / OVER / ROT ─────────────────────────────────────────────────
# push 1, push 2, swap → TOS=1, emit → 1
"$MKTENB" "$TMP/t10a.tenb" 02 01 02 02 07 50 01
check "swap_emit_bottom" "$TMP/t10a.tenb" "$(printf '\x01')"
# push 10, push 20, over → [10,20,10], emit top → 10
"$MKTENB" "$TMP/t10b.tenb" 02 0A 02 14 08 50 01
check "over_emit_copy" "$TMP/t10b.tenb" "$(printf '\x0a')"

# ── 11. JZ (conditional jump — branch taken) ──────────────────────────────
# push 0, JZ → skip over PUSH8 42 + EMIT, land on HALT
# Bytecode layout:
#  0: 02 00          PUSH8 0
#  2: 41 08 00       JZ 8           (jump to offset 8)
#  5: 02 2A          PUSH8 42
#  7: 50             EMIT
#  8: 01             HALT
"$MKTENB" "$TMP/t11.tenb" 02 00 41 08 00 02 2A 50 01
check "jz_taken_no_emit" "$TMP/t11.tenb" ""

# ── 12. JZ (branch NOT taken) ─────────────────────────────────────────────
# push 1 (non-zero), JZ skipped, emit 42, halt
#  0: 02 01          PUSH8 1
#  2: 41 08 00       JZ 8           (not taken)
#  5: 02 2A          PUSH8 42
#  7: 50             EMIT
#  8: 01             HALT
"$MKTENB" "$TMP/t12.tenb" 02 01 41 08 00 02 2A 50 01
check "jz_not_taken_emits_42" "$TMP/t12.tenb" "$(printf '\x2a')"

# ── 13. CALL + RET ────────────────────────────────────────────────────────
# Main: push 7, call double (offset 10), emit, halt
# double: DUP, ADD (= ×2), RET
# Layout:
#  0: 02 07          PUSH8 7
#  2: 43 0A 00       CALL 10
#  5: 50             EMIT
#  6: 02 63          PUSH8 99 (dead code, just padding to reach offset 10)
#  8: 50             EMIT     (dead code)
#  9: 01             HALT
# 10: 06             DUP
# 11: 10             ADD
# 12: 44             RET
"$MKTENB" "$TMP/t13.tenb" 02 07 43 0A 00 50 01 00 00 00 06 10 44
#                           0  1  2  3  4  5  6  7  8  9 10 11 12
check "call_ret_double_7=14" "$TMP/t13.tenb" "$(printf '\x0e')"

# ── 14. JNZ loop (countdown 5→1, emitting each) ───────────────────────────
# Bytecode:
#  0: 02 05          PUSH8 5
#  2: 06             DUP
#  3: 50             EMIT
#  4: 17             DEC
#  5: 06             DUP
#  6: 02 00          PUSH8 0
#  8: 23             GT
#  9: 42 02 00       JNZ 2
# 12: 05             POP
# 13: 01             HALT
"$MKTENB" "$TMP/t14.tenb" 02 05 06 50 17 06 02 00 23 42 02 00 05 01
check "loop_countdown_5_to_1" "$TMP/t14.tenb" "$(printf '\x05\x04\x03\x02\x01')"

# ── 15. Memory STORE32 + LOAD32 ───────────────────────────────────────────
# push 0xAB, push addr=0, STORE32, push addr=0, LOAD32, emit, halt
# 0xAB = 171
# PUSH8 0xAB: 0xAB as int8 = -85; we need PUSH16 for 171=0xAB
# Actually: 0xAB = 171. As int8 (signed): -85. So PUSH8 0xAB gives -85 on stack.
# Let's use 0x60 = 96 which is fine as signed int8.
# store 96 at addr 0, load, emit → 96
"$MKTENB" "$TMP/t15.tenb" 02 60 02 00 38 02 00 30 50 01
#                    PUSH8 96  PUSH8 0  STORE32  PUSH8 0  LOAD32  EMIT  HALT
check "store32_load32_roundtrip" "$TMP/t15.tenb" "$(printf '\x60')"

# ── 16. PUSH16 ────────────────────────────────────────────────────────────
# PUSH16 300 (0x012C), emit low byte (0x2C = 44), halt
"$MKTENB" "$TMP/t16.tenb" 03 2C 01 50 01
check "push16_300_emit_low_44" "$TMP/t16.tenb" "$(printf '\x2c')"

# ── 17. INC + DEC ─────────────────────────────────────────────────────────
# push 10, inc (→11), dec (→10), emit
"$MKTENB" "$TMP/t17.tenb" 02 0A 16 17 50 01
check "inc_dec_roundtrip" "$TMP/t17.tenb" "$(printf '\x0a')"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
