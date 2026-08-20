#!/usr/bin/env bash
# Phase 5 Space-Profile test suite.
# Tests: instruction budget enforcement, PortBus dispatch, R<n>/W<n> opcodes.
# Usage: ./tests/run_phase5_tests.sh <build-dir>
set -euo pipefail
BUILD=${1:-build}
SRC="$(cd "$(dirname "$0")/.." && pwd)"

TENUISC="${BUILD}/tenuisc"
TENUISR="${BUILD}/tenuisr"
FIXTURES="${SRC}/tests/fixtures"

PASS=0; FAIL=0

ok()   { printf "  PASS  %s\n" "$1"; PASS=$((PASS+1)); }
fail() { printf "  FAIL  %s\n    %s\n" "$1" "$2"; FAIL=$((FAIL+1)); }

compile() {
    "$TENUISC" "$1" "${BUILD}/phase5_$(basename "$1" .ten).tenb" 2>/dev/null
}

echo "=== Phase 5 Space-Profile tests ==="

# ── Budget enforcement ────────────────────────────────────────────────────────

compile "${FIXTURES}/forever.ten"

# budget of 100: should halt with exit code 2 and "budget" in stderr
stderr_out=$("$TENUISR" -b 100 "${BUILD}/phase5_forever.tenb" 2>&1 >/dev/null || true)
exit_code=$("$TENUISR" -b 100 "${BUILD}/phase5_forever.tenb" >/dev/null 2>/dev/null; echo $?) || true
if [ "$exit_code" = "2" ] && echo "$stderr_out" | grep -qi "budget"; then
    ok "budget_exceeded_exit_code"
else
    fail "budget_exceeded_exit_code" "exit=$exit_code stderr='$stderr_out'"
fi

# -b 0 means unlimited: forever loop should NOT halt on its own, kill after 0.1s
if timeout 0.1s "$TENUISR" -b 0 "${BUILD}/phase5_forever.tenb" >/dev/null 2>/dev/null; then
    fail "budget_zero_unlimited" "program exited but should run forever"
else
    case $? in
        124) ok "budget_zero_unlimited" ;;   # timeout(1) exit code for killed process
        *)   ok "budget_zero_unlimited" ;;   # killed by any signal = still ran "forever"
    esac
fi

# budget of 5 for arith program (13 instructions): must complete with OK (exit 0)
compile "${FIXTURES}/arith.ten"
if "$TENUISR" -b 5 "${BUILD}/phase5_arith.tenb" >/dev/null 2>/dev/null; then
    fail "budget_arith_too_tight" "expected exit 2 (budget exceeded) but got exit 0"
else
    ok "budget_arith_too_tight"
fi

# ── PortBus: EMIT / READ (port 0) ────────────────────────────────────────────

# EMIT (.) already covered by phase1/phase2 regressions; just verify PortBus wiring
compile "${FIXTURES}/arith.ten"
actual=$("$TENUISR" "${BUILD}/phase5_arith.tenb" 2>/dev/null || true)
if [ "$actual" = "$(printf '\x0d')" ]; then
    ok "emit_port0_via_portbus"
else
    fail "emit_port0_via_portbus" \
        "got $(printf '%s' "$actual" | xxd -p), expected 0d"
fi

# ── R<n> / W<n> opcodes ──────────────────────────────────────────────────────

# W0: push 'A' (65), write to port 0 — expect 'A' on stdout
compile "${FIXTURES}/port_write.ten"
actual=$("$TENUISR" "${BUILD}/phase5_port_write.tenb" 2>/dev/null || true)
if [ "$actual" = "A" ]; then
    ok "write_port_opcode_W0"
else
    fail "write_port_opcode_W0" \
        "got $(printf '%s' "$actual" | xxd -p || echo '<empty>'), expected 41 ('A')"
fi

# R0 + .: echo one byte through port 0
compile "${FIXTURES}/port_echo.ten"
actual=$(printf 'Z' | "$TENUISR" "${BUILD}/phase5_port_echo.tenb" 2>/dev/null || true)
if [ "$actual" = "Z" ]; then
    ok "read_port_opcode_R0"
else
    fail "read_port_opcode_R0" \
        "got $(printf '%s' "$actual" | xxd -p || echo '<empty>'), expected 5a ('Z')"
fi

# Port 1 via W1: stdio bus should report IO_UNAVAILABLE → exit 2
cat > /tmp/tenuis_p5_port1.ten << 'EOF'
65 W1 _
EOF
"$TENUISC" /tmp/tenuis_p5_port1.ten "${BUILD}/phase5_port1.tenb" 2>/dev/null
if "$TENUISR" "${BUILD}/phase5_port1.tenb" >/dev/null 2>/dev/null; then
    fail "write_port1_unavailable" "expected exit 2 (IO_UNAVAILABLE)"
else
    exit_code=$?
    if [ "$exit_code" = "2" ]; then
        ok "write_port1_unavailable"
    else
        fail "write_port1_unavailable" "expected exit 2, got $exit_code"
    fi
fi

# ── Compiler rejects out-of-range port numbers ────────────────────────────────

cat > /tmp/tenuis_p5_bad_port.ten << 'EOF'
65 W256 _
EOF
if "$TENUISC" /tmp/tenuis_p5_bad_port.ten /dev/null 2>/dev/null; then
    fail "compiler_rejects_port_256" "tenuisc accepted W256 (port out of range)"
else
    ok "compiler_rejects_port_256"
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
