#!/usr/bin/env bash
# Phase 6 Hybrid-mode test suite.
# Tests: embedded safe-mode fallback, uplink override, corrupted uplink rejection.
# Usage: ./tests/run_phase6_tests.sh <build-dir>
set -euo pipefail
BUILD=${1:-build}
SRC="$(cd "$(dirname "$0")/.." && pwd)"

TENUISC="${BUILD}/tenuisc"
HYBRID="${BUILD}/arith_hybrid"   # safe-mode = arith → outputs 0x0d
FIXTURES="${SRC}/tests/fixtures"

PASS=0; FAIL=0

ok()   { printf "  PASS  %s\n" "$1"; PASS=$((PASS+1)); }
fail() { printf "  FAIL  %s\n    %s\n" "$1" "$2"; FAIL=$((FAIL+1)); }

ARITH_OUT="$(printf '\x0d')"

echo "=== Phase 6 Hybrid-mode tests ==="

# ── 1. No uplink → embedded safe-mode runs ───────────────────────────────────
actual=$("$HYBRID" 2>/dev/null || true)
if [ "$actual" = "$ARITH_OUT" ]; then
    ok "no_uplink_uses_embedded"
else
    fail "no_uplink_uses_embedded" \
        "got $(printf '%s' "$actual" | xxd -p || echo '<empty>'), expected 0d"
fi

# ── 2. Valid uplink → uplink program runs (branch.ten → "Y") ─────────────────
"$TENUISC" "${FIXTURES}/branch.ten" "${BUILD}/uplink_branch.tenb" 2>/dev/null
actual=$("$HYBRID" "${BUILD}/uplink_branch.tenb" 2>/dev/null || true)
if [ "$actual" = "Y" ]; then
    ok "valid_uplink_overrides_embedded"
else
    fail "valid_uplink_overrides_embedded" \
        "got $(printf '%s' "$actual" | xxd -p || echo '<empty>'), expected 59 ('Y')"
fi

# ── 3. Uplink program runs with correct output (loop.ten → ABCDE) ────────────
"$TENUISC" "${FIXTURES}/loop.ten" "${BUILD}/uplink_loop.tenb" 2>/dev/null
actual=$("$HYBRID" "${BUILD}/uplink_loop.tenb" 2>/dev/null || true)
if [ "$actual" = "ABCDE" ]; then
    ok "valid_uplink_loop_output"
else
    fail "valid_uplink_loop_output" \
        "got '$(printf '%s' "$actual" | xxd -p || echo '<empty>')'"
fi

# ── 4. Corrupted uplink → fallback to embedded safe-mode ─────────────────────
echo "this is not a tenuis binary" > /tmp/tenuis_corrupt.tenb
actual=$("$HYBRID" /tmp/tenuis_corrupt.tenb 2>/dev/null || true)
if [ "$actual" = "$ARITH_OUT" ]; then
    ok "corrupted_uplink_fallback"
else
    fail "corrupted_uplink_fallback" \
        "got $(printf '%s' "$actual" | xxd -p || echo '<empty>'), expected 0d (safe-mode)"
fi

# ── 5. Missing uplink file → fallback to embedded safe-mode ──────────────────
actual=$("$HYBRID" /tmp/tenuis_nonexistent_xyz.tenb 2>/dev/null || true)
if [ "$actual" = "$ARITH_OUT" ]; then
    ok "missing_uplink_fallback"
else
    fail "missing_uplink_fallback" \
        "got $(printf '%s' "$actual" | xxd -p || echo '<empty>'), expected 0d (safe-mode)"
fi

# ── 6. Fallback message goes to stderr, not stdout ───────────────────────────
stdout=$("$HYBRID" /tmp/tenuis_corrupt.tenb 2>/dev/null || true)
stderr=$("$HYBRID" /tmp/tenuis_corrupt.tenb 2>&1 >/dev/null || true)
if [ "$stdout" = "$ARITH_OUT" ] && echo "$stderr" | grep -qi "fallback\|embedded\|rejected"; then
    ok "fallback_message_on_stderr"
else
    fail "fallback_message_on_stderr" \
        "stdout=$(printf '%s' "$stdout" | xxd -p) stderr='$stderr'"
fi

# ── 7. Budget enforced on uplink program too ──────────────────────────────────
"$TENUISC" "${FIXTURES}/forever.ten" "${BUILD}/uplink_forever.tenb" 2>/dev/null
if "$HYBRID" -b 50 "${BUILD}/uplink_forever.tenb" >/dev/null 2>/dev/null; then
    fail "budget_enforced_on_uplink" "expected exit 2 (budget exceeded)"
else
    exit_code=$?
    if [ "$exit_code" = "2" ]; then
        ok "budget_enforced_on_uplink"
    else
        fail "budget_enforced_on_uplink" "expected exit 2, got $exit_code"
    fi
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
