#include "vm.h"
#include "io.h"
#include <cstring>

void vm_init(VM& vm,
             const uint8_t* code,  uint32_t code_len,
             const uint8_t* dinit, uint32_t data_len,
             uint16_t entry,
             const VMConfig& cfg)
{
    memcpy(vm.code, code, code_len);
    vm.code_size = code_len;
    memset(vm.data, 0, sizeof(vm.data));
    if (dinit && data_len)
        memcpy(vm.data, dinit, data_len);
    vm.pc                    = entry;
    vm.sp                    = -1;
    vm.rsp                   = -1;
    vm.halt_reason           = HaltReason::OK;
    vm.instruction_limit     = cfg.instruction_limit;
    vm.instructions_executed = 0u;
    vm.ports                 = cfg.ports;
}

VMResult vm_run(VM& vm) {
    // Local copies for tighter register allocation in the hot loop.
    uint8_t*        code      = vm.code;
    uint8_t*        data      = vm.data;
    int32_t*        stack     = vm.stack;
    uint16_t*       rs        = vm.rstack;
    const uint32_t  code_size = vm.code_size;
    int32_t         sp        = vm.sp;
    int32_t         rsp       = vm.rsp;
    uint32_t        pc        = vm.pc;
    uint32_t        ic        = 0u;
    const uint32_t  budget    = vm.instruction_limit;
    const PortBus   ports     = vm.ports;

    vm.halt_reason = HaltReason::OK;

    for (;;) {
        if (budget && ic >= budget) { vm.halt_reason = HaltReason::BUDGET_EXCEEDED; goto halt; }
        if (pc >= code_size) { vm.halt_reason = HaltReason::PC_OUT_OF_RANGE; goto halt; }
        const uint8_t op = code[pc++];
        ++ic;
        switch (op) {

        // ── Stack ────────────────────────────────────────────────────────────
        case 0x00: break; // NOP

        case 0x01: goto halt; // HALT — clean exit, halt_reason stays OK

        case 0x02: // PUSH8  ( -- n )   sign-extended int8
            if (pc >= code_size) { vm.halt_reason = HaltReason::PC_OUT_OF_RANGE; goto halt; }
            if (sp + 1 >= (int32_t)TENUIS_STACK_DEPTH) { vm.halt_reason = HaltReason::STACK_OVERFLOW; goto halt; }
            stack[++sp] = static_cast<int32_t>(static_cast<int8_t>(code[pc++]));
            break;

        case 0x03: // PUSH16  ( -- n )   sign-extended int16 LE
            if (pc + 1 >= code_size) { vm.halt_reason = HaltReason::PC_OUT_OF_RANGE; goto halt; }
            if (sp + 1 >= (int32_t)TENUIS_STACK_DEPTH) { vm.halt_reason = HaltReason::STACK_OVERFLOW; goto halt; }
            {
                const int16_t v = static_cast<int16_t>(
                    static_cast<uint16_t>(code[pc]) | (static_cast<uint16_t>(code[pc+1]) << 8));
                pc += 2;
                stack[++sp] = static_cast<int32_t>(v);
            }
            break;

        case 0x04: // PUSH32  ( -- n )   int32 LE
            if (pc + 3 >= code_size) { vm.halt_reason = HaltReason::PC_OUT_OF_RANGE; goto halt; }
            if (sp + 1 >= (int32_t)TENUIS_STACK_DEPTH) { vm.halt_reason = HaltReason::STACK_OVERFLOW; goto halt; }
            {
                const int32_t v = static_cast<int32_t>(
                    static_cast<uint32_t>(code[pc])
                    | (static_cast<uint32_t>(code[pc+1]) << 8)
                    | (static_cast<uint32_t>(code[pc+2]) << 16)
                    | (static_cast<uint32_t>(code[pc+3]) << 24));
                pc += 4;
                stack[++sp] = v;
            }
            break;

        case 0x05: // POP  ( n -- )
            if (sp < 0) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            --sp;
            break;

        case 0x06: // DUP  ( n -- n n )
            if (sp < 0) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            if (sp + 1 >= (int32_t)TENUIS_STACK_DEPTH) { vm.halt_reason = HaltReason::STACK_OVERFLOW; goto halt; }
            stack[sp+1] = stack[sp];
            ++sp;
            break;

        case 0x07: // SWAP  ( a b -- b a )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            { const int32_t t = stack[sp]; stack[sp] = stack[sp-1]; stack[sp-1] = t; }
            break;

        case 0x08: // OVER  ( a b -- a b a )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            if (sp + 1 >= (int32_t)TENUIS_STACK_DEPTH) { vm.halt_reason = HaltReason::STACK_OVERFLOW; goto halt; }
            stack[sp+1] = stack[sp-1];
            ++sp;
            break;

        case 0x09: // ROT  ( a b c -- b c a )
            if (sp < 2) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            {
                const int32_t a = stack[sp-2];
                stack[sp-2] = stack[sp-1];
                stack[sp-1] = stack[sp];
                stack[sp]   = a;
            }
            break;

        // ── Arithmetic ───────────────────────────────────────────────────────
        case 0x10: // ADD  ( a b -- a+b )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            stack[sp-1] += stack[sp]; --sp;
            break;

        case 0x11: // SUB  ( a b -- a-b )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            stack[sp-1] -= stack[sp]; --sp;
            break;

        case 0x12: // MUL  ( a b -- a*b )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            stack[sp-1] *= stack[sp]; --sp;
            break;

        case 0x13: // DIV  ( a b -- a/b )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            if (stack[sp] == 0) { vm.halt_reason = HaltReason::DIVISION_BY_ZERO; goto halt; }
            stack[sp-1] /= stack[sp]; --sp;
            break;

        case 0x14: // MOD  ( a b -- a%b )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            if (stack[sp] == 0) { vm.halt_reason = HaltReason::DIVISION_BY_ZERO; goto halt; }
            stack[sp-1] %= stack[sp]; --sp;
            break;

        case 0x15: // NEG  ( a -- -a )
            if (sp < 0) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            stack[sp] = -stack[sp];
            break;

        case 0x16: // INC  ( a -- a+1 )
            if (sp < 0) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            ++stack[sp];
            break;

        case 0x17: // DEC  ( a -- a-1 )
            if (sp < 0) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            --stack[sp];
            break;

        // ── Bitwise ──────────────────────────────────────────────────────────
        case 0x18: // AND  ( a b -- a&b )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            stack[sp-1] &= stack[sp]; --sp;
            break;

        case 0x19: // OR  ( a b -- a|b )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            stack[sp-1] |= stack[sp]; --sp;
            break;

        case 0x1A: // XOR  ( a b -- a^b )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            stack[sp-1] ^= stack[sp]; --sp;
            break;

        case 0x1B: // BITNOT  ( a -- ~a )
            if (sp < 0) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            stack[sp] = ~stack[sp];
            break;

        case 0x1C: // SHL  ( a n -- a<<n )   logical
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            {
                const uint32_t shift = static_cast<uint32_t>(stack[sp--]);
                stack[sp] = (shift >= 32u)
                    ? 0
                    : static_cast<int32_t>(static_cast<uint32_t>(stack[sp]) << shift);
            }
            break;

        case 0x1D: // SHR  ( a n -- a>>n )   logical (unsigned fill)
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            {
                const uint32_t shift = static_cast<uint32_t>(stack[sp--]);
                stack[sp] = (shift >= 32u)
                    ? 0
                    : static_cast<int32_t>(static_cast<uint32_t>(stack[sp]) >> shift);
            }
            break;

        case 0x1E: // SAR  ( a n -- a>>n )   arithmetic (sign fill)
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            {
                uint32_t shift = static_cast<uint32_t>(stack[sp--]);
                if (shift >= 32u) shift = 31u;
                stack[sp] >>= static_cast<int>(shift);
            }
            break;

        // ── Comparison ───────────────────────────────────────────────────────
        case 0x20: // EQ  ( a b -- flag )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            stack[sp-1] = (stack[sp-1] == stack[sp]) ? 1 : 0; --sp;
            break;

        case 0x21: // NEQ  ( a b -- flag )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            stack[sp-1] = (stack[sp-1] != stack[sp]) ? 1 : 0; --sp;
            break;

        case 0x22: // LT  ( a b -- flag )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            stack[sp-1] = (stack[sp-1] < stack[sp]) ? 1 : 0; --sp;
            break;

        case 0x23: // GT  ( a b -- flag )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            stack[sp-1] = (stack[sp-1] > stack[sp]) ? 1 : 0; --sp;
            break;

        case 0x24: // LE  ( a b -- flag )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            stack[sp-1] = (stack[sp-1] <= stack[sp]) ? 1 : 0; --sp;
            break;

        case 0x25: // GE  ( a b -- flag )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            stack[sp-1] = (stack[sp-1] >= stack[sp]) ? 1 : 0; --sp;
            break;

        // ── Memory ───────────────────────────────────────────────────────────
        case 0x30: // LOAD32  ( addr -- val )
            if (sp < 0) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            {
                const uint32_t addr = static_cast<uint32_t>(stack[sp]);
                if (addr > TENUIS_DATA_SIZE - 4u) { vm.halt_reason = HaltReason::MEM_OUT_OF_RANGE; goto halt; }
                stack[sp] = static_cast<int32_t>(
                    static_cast<uint32_t>(data[addr])
                    | (static_cast<uint32_t>(data[addr+1]) << 8)
                    | (static_cast<uint32_t>(data[addr+2]) << 16)
                    | (static_cast<uint32_t>(data[addr+3]) << 24));
            }
            break;

        case 0x31: // LOAD8  ( addr -- val )  zero-extended
            if (sp < 0) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            {
                const uint32_t addr = static_cast<uint32_t>(stack[sp]);
                if (addr >= TENUIS_DATA_SIZE) { vm.halt_reason = HaltReason::MEM_OUT_OF_RANGE; goto halt; }
                stack[sp] = static_cast<int32_t>(data[addr]);
            }
            break;

        case 0x32: // LOAD16  ( addr -- val )  zero-extended
            if (sp < 0) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            {
                const uint32_t addr = static_cast<uint32_t>(stack[sp]);
                if (addr > TENUIS_DATA_SIZE - 2u) { vm.halt_reason = HaltReason::MEM_OUT_OF_RANGE; goto halt; }
                stack[sp] = static_cast<int32_t>(
                    static_cast<uint32_t>(data[addr])
                    | (static_cast<uint32_t>(data[addr+1]) << 8));
            }
            break;

        case 0x38: // STORE32  ( val addr -- )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            {
                const uint32_t addr = static_cast<uint32_t>(stack[sp]);
                const int32_t  val  = stack[sp-1];
                if (addr > TENUIS_DATA_SIZE - 4u) { vm.halt_reason = HaltReason::MEM_OUT_OF_RANGE; goto halt; }
                sp -= 2;
                data[addr]   = static_cast<uint8_t>(val);
                data[addr+1] = static_cast<uint8_t>(val >> 8);
                data[addr+2] = static_cast<uint8_t>(val >> 16);
                data[addr+3] = static_cast<uint8_t>(val >> 24);
            }
            break;

        case 0x39: // STORE8  ( val addr -- )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            {
                const uint32_t addr = static_cast<uint32_t>(stack[sp]);
                const int32_t  val  = stack[sp-1];
                if (addr >= TENUIS_DATA_SIZE) { vm.halt_reason = HaltReason::MEM_OUT_OF_RANGE; goto halt; }
                sp -= 2;
                data[addr] = static_cast<uint8_t>(val);
            }
            break;

        case 0x3A: // STORE16  ( val addr -- )
            if (sp < 1) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            {
                const uint32_t addr = static_cast<uint32_t>(stack[sp]);
                const int32_t  val  = stack[sp-1];
                if (addr > TENUIS_DATA_SIZE - 2u) { vm.halt_reason = HaltReason::MEM_OUT_OF_RANGE; goto halt; }
                sp -= 2;
                data[addr]   = static_cast<uint8_t>(val);
                data[addr+1] = static_cast<uint8_t>(val >> 8);
            }
            break;

        // ── Control Flow ─────────────────────────────────────────────────────
        case 0x40: // JMP  ( -- )   absolute uint16 LE target
            if (pc + 1 >= code_size) { vm.halt_reason = HaltReason::PC_OUT_OF_RANGE; goto halt; }
            pc = static_cast<uint32_t>(code[pc] | (static_cast<uint32_t>(code[pc+1]) << 8));
            break;

        case 0x41: // JZ  ( flag -- )   jump if flag == 0
            if (sp < 0) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            if (pc + 1 >= code_size) { vm.halt_reason = HaltReason::PC_OUT_OF_RANGE; goto halt; }
            {
                const uint32_t target = static_cast<uint32_t>(
                    code[pc] | (static_cast<uint32_t>(code[pc+1]) << 8));
                const int32_t flag = stack[sp--];
                pc = (flag == 0) ? target : pc + 2;
            }
            break;

        case 0x42: // JNZ  ( flag -- )   jump if flag != 0
            if (sp < 0) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            if (pc + 1 >= code_size) { vm.halt_reason = HaltReason::PC_OUT_OF_RANGE; goto halt; }
            {
                const uint32_t target = static_cast<uint32_t>(
                    code[pc] | (static_cast<uint32_t>(code[pc+1]) << 8));
                const int32_t flag = stack[sp--];
                pc = (flag != 0) ? target : pc + 2;
            }
            break;

        case 0x43: // CALL  ( -- )   push PC+2 to return stack, jump to target
            if (pc + 1 >= code_size) { vm.halt_reason = HaltReason::PC_OUT_OF_RANGE; goto halt; }
            if (rsp + 1 >= (int32_t)TENUIS_RSTACK_DEPTH) { vm.halt_reason = HaltReason::RSTACK_OVERFLOW; goto halt; }
            {
                const uint32_t target = static_cast<uint32_t>(
                    code[pc] | (static_cast<uint32_t>(code[pc+1]) << 8));
                rs[++rsp] = static_cast<uint16_t>(pc + 2);
                pc = target;
            }
            break;

        case 0x44: // RET  ( -- )   pop return stack, jump there
            if (rsp < 0) { vm.halt_reason = HaltReason::RSTACK_UNDERFLOW; goto halt; }
            pc = rs[rsp--];
            break;

        // ── I/O ──────────────────────────────────────────────────────────────
        case 0x50: // EMIT  ( val -- )   write low byte to port 0
            if (sp < 0) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            {
                const int err = ports.write_fn
                    ? ports.write_fn(ports.context, 0u, static_cast<uint8_t>(stack[sp--]))
                    : -1;
                if (err == -1) { vm.halt_reason = HaltReason::IO_UNAVAILABLE; goto halt; }
                if (err == -2) { vm.halt_reason = HaltReason::IO_TIMEOUT;     goto halt; }
                if (err <   0) { vm.halt_reason = HaltReason::IO_FAULT;       goto halt; }
            }
            break;

        case 0x51: // READ  ( -- val )   read one byte from port 0, zero-extended
            if (sp + 1 >= (int32_t)TENUIS_STACK_DEPTH) { vm.halt_reason = HaltReason::STACK_OVERFLOW; goto halt; }
            {
                uint8_t byte = 0xFFu;
                const int err = ports.read_fn
                    ? ports.read_fn(ports.context, 0u, &byte)
                    : -1;
                if (err == -1) { vm.halt_reason = HaltReason::IO_UNAVAILABLE; goto halt; }
                if (err == -2) { vm.halt_reason = HaltReason::IO_TIMEOUT;     goto halt; }
                if (err <   0) { vm.halt_reason = HaltReason::IO_FAULT;       goto halt; }
                stack[++sp] = static_cast<int32_t>(byte);
            }
            break;

        case 0x52: // WRITE_PORT  ( val -- )   write low byte to port N (inline byte)
            if (sp < 0) { vm.halt_reason = HaltReason::STACK_UNDERFLOW; goto halt; }
            if (pc >= code_size) { vm.halt_reason = HaltReason::PC_OUT_OF_RANGE; goto halt; }
            {
                const uint8_t port = code[pc++];
                const int err = ports.write_fn
                    ? ports.write_fn(ports.context, port, static_cast<uint8_t>(stack[sp--]))
                    : -1;
                if (err == -1) { vm.halt_reason = HaltReason::IO_UNAVAILABLE; goto halt; }
                if (err == -2) { vm.halt_reason = HaltReason::IO_TIMEOUT;     goto halt; }
                if (err <   0) { vm.halt_reason = HaltReason::IO_FAULT;       goto halt; }
            }
            break;

        case 0x53: // READ_PORT  ( -- val )   read one byte from port N (inline byte)
            if (sp + 1 >= (int32_t)TENUIS_STACK_DEPTH) { vm.halt_reason = HaltReason::STACK_OVERFLOW; goto halt; }
            if (pc >= code_size) { vm.halt_reason = HaltReason::PC_OUT_OF_RANGE; goto halt; }
            {
                const uint8_t port = code[pc++];
                uint8_t byte = 0xFFu;
                const int err = ports.read_fn
                    ? ports.read_fn(ports.context, port, &byte)
                    : -1;
                if (err == -1) { vm.halt_reason = HaltReason::IO_UNAVAILABLE; goto halt; }
                if (err == -2) { vm.halt_reason = HaltReason::IO_TIMEOUT;     goto halt; }
                if (err <   0) { vm.halt_reason = HaltReason::IO_FAULT;       goto halt; }
                stack[++sp] = static_cast<int32_t>(byte);
            }
            break;

        default:
            vm.halt_reason = HaltReason::ILLEGAL_OPCODE;
            goto halt;
        }
    }

halt:
    vm.sp                    = sp;
    vm.rsp                   = rsp;
    vm.pc                    = static_cast<uint16_t>(pc);
    vm.instructions_executed = ic;
    return { vm.halt_reason, ic };
}

const char* halt_reason_str(HaltReason r) {
    switch (r) {
        case HaltReason::OK:               return "ok";
        case HaltReason::STACK_UNDERFLOW:  return "fault: stack underflow";
        case HaltReason::STACK_OVERFLOW:   return "fault: stack overflow";
        case HaltReason::RSTACK_UNDERFLOW: return "fault: return stack underflow";
        case HaltReason::RSTACK_OVERFLOW:  return "fault: return stack overflow";
        case HaltReason::PC_OUT_OF_RANGE:  return "fault: PC out of range";
        case HaltReason::MEM_OUT_OF_RANGE: return "fault: memory out of range";
        case HaltReason::DIVISION_BY_ZERO: return "fault: division by zero";
        case HaltReason::ILLEGAL_OPCODE:   return "fault: illegal opcode";
        case HaltReason::BUDGET_EXCEEDED:  return "fault: budget exceeded";
        case HaltReason::IO_UNAVAILABLE:   return "fault: I/O port unavailable";
        case HaltReason::IO_TIMEOUT:       return "fault: I/O timeout";
        case HaltReason::IO_FAULT:         return "fault: I/O fault";
    }
    return "fault: unknown";
}
