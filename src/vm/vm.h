#pragma once
#include <cstdint>
#include "config.h"

enum class HaltReason : uint8_t {
    OK = 0,
    STACK_UNDERFLOW,
    STACK_OVERFLOW,
    RSTACK_UNDERFLOW,
    RSTACK_OVERFLOW,
    PC_OUT_OF_RANGE,
    MEM_OUT_OF_RANGE,
    DIVISION_BY_ZERO,
    ILLEGAL_OPCODE,
    BUDGET_EXCEEDED,
    IO_UNAVAILABLE,
    IO_TIMEOUT,
    IO_FAULT,
};

struct VMResult {
    HaltReason reason;
    uint32_t   instructions_executed;
};

// Port bus: pair of function pointers for deterministic I/O dispatch.
// Return 0 on success, -1 = unavailable, -2 = timeout, -3 = fault.
struct PortBus {
    void* context;
    int (*write_fn)(void* ctx, uint8_t port, uint8_t value);
    int (*read_fn) (void* ctx, uint8_t port, uint8_t* value);
};

struct VMConfig {
    uint32_t instruction_limit;   // 0 = unlimited
    PortBus  ports;
};

struct VM {
    uint8_t  code[TENUIS_CODE_SIZE];
    uint8_t  data[TENUIS_DATA_SIZE];
    int32_t  stack[TENUIS_STACK_DEPTH];
    uint16_t rstack[TENUIS_RSTACK_DEPTH];
    uint32_t code_size;
    uint16_t pc;
    int32_t  sp;
    int32_t  rsp;
    HaltReason halt_reason;
    uint32_t   instruction_limit;
    uint32_t   instructions_executed;
    PortBus    ports;
};

void        vm_init(VM& vm, const uint8_t* code, uint32_t code_len,
                    const uint8_t* data_init, uint32_t data_len, uint16_t entry,
                    const VMConfig& cfg);
VMResult    vm_run(VM& vm);
const char* halt_reason_str(HaltReason r);
