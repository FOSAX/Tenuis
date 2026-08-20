# Tenuis Space Profile

Version: 1.0
Status: Normative

## Overview

The Space Profile is a set of runtime extensions designed for autonomous operation in resource-constrained, fault-prone environments. It adds three capabilities on top of the base VM:

1. A structured halt code returned on every execution — never just an exit code.
2. A hard instruction budget that guarantees program termination.
3. A configurable I/O port bus that decouples the program from the physical hardware.

These three features are independent but designed to work together. A spacecraft integrator configures all three once, at the point where the VM is initialised, and the program code itself does not need to know about the hardware.

## Types

### HaltReason

```cpp
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
```

Every call to `vm_run()` returns a `VMResult` struct carrying one of these values. `OK` means the program reached a `HALT` instruction cleanly. All other values indicate an abnormal stop.

The string form of any `HaltReason` is available via:

```cpp
const char* halt_reason_str(HaltReason r);
```

Example output: `"fault: budget exceeded"`, `"fault: I/O port unavailable"`.

### VMResult

```cpp
struct VMResult {
    HaltReason reason;
    uint32_t   instructions_executed;
};
```

`instructions_executed` is the number of opcodes fetched and dispatched during the run, regardless of how the run ended. This count is exact and deterministic: the same program on the same input with the same budget will always produce the same count. It is suitable for logging to a flight journal or for comparing execution profiles between ground simulation and flight.

### PortBus

```cpp
struct PortBus {
    void* context;
    int (*write_fn)(void* ctx, uint8_t port, uint8_t value);
    int (*read_fn) (void* ctx, uint8_t port, uint8_t* value);
};
```

The `context` pointer is passed as the first argument to every call. It can be used to carry a pointer to hardware registers, a mock structure for simulation, or anything the integrator needs.

Both function pointers may be null. A null pointer is treated as `IO_UNAVAILABLE` for all ports.

Return value contract for both functions:

| Return value | Meaning | VM halts with |
|---|---|---|
| 0 | Success | (continues) |
| -1 | No handler for this port | `IO_UNAVAILABLE` |
| -2 | Handler registered, no response | `IO_TIMEOUT` |
| any other negative | Hardware fault | `IO_FAULT` |

### VMConfig

```cpp
struct VMConfig {
    uint32_t instruction_limit;   // 0 = unlimited
    PortBus  ports;
};
```

`VMConfig` is passed once to `vm_init()`. The values are copied into the VM struct and persist for the lifetime of the execution. To change the budget or bus between runs, initialise a new VM or call `vm_init()` again.

## Instruction Budget

When `instruction_limit` is non-zero, the VM counts every opcode it fetches. When the count reaches the limit, the VM halts immediately with `HaltReason::BUDGET_EXCEEDED` before fetching the next opcode.

```
budget = 5, program has infinite loop:
  opcode 1 fetched → ic = 1, check: 1 >= 5? no
  opcode 2 fetched → ic = 2, check: 2 >= 5? no
  ...
  opcode 5 fetched → ic = 5, check: 5 >= 5? no, execute
  check at top of loop: 5 >= 5? yes → BUDGET_EXCEEDED
```

The check happens at the top of the loop, before the next fetch. Exactly `instruction_limit` opcodes are executed before the halt.

`instruction_limit = 0` disables the budget entirely. The VM runs until it hits `HALT`, a fault, or the instruction count wraps around `UINT32_MAX` (not a practical concern).

Command-line usage in `tenuisr`:

```
tenuisr -b 50000 program.tenb
```

API usage:

```cpp
VMConfig cfg = { 50000u, tenuis_stdio_bus() };
vm_init(vm, code, code_len, nullptr, 0u, entry, cfg);
VMResult result = vm_run(vm);
if (result.reason == HaltReason::BUDGET_EXCEEDED) {
    log_fault("budget exceeded after %u instructions", result.instructions_executed);
}
```

## Port Bus

### Port 0

Port 0 is the default I/O channel. The source characters `.` (EMIT, opcode `0x50`) and backtick (READ, opcode `0x51`) always target port 0. Programs that do not need multi-port I/O can use only these two instructions and never reference port numbers explicitly.

### WRITE_PORT and READ_PORT

Opcodes `0x52` (WRITE_PORT) and `0x53` (READ_PORT) address an arbitrary port. The port number is a single byte encoded inline in the bytecode immediately after the opcode.

Source syntax: `W<n>` and `R<n>` where `n` is a decimal integer in the range 0-255. The compiler rejects values outside this range.

```
// Push sensor value, write to telemetry port
(read_sensor) W7 _

// Read command byte from uplink port
R3 (process_command) _
```

### Implementing a Custom Bus

```cpp
struct MyBusContext {
    volatile uint8_t* uart_dr;   // UART data register
    volatile uint8_t* spi_dr;    // SPI data register
};

static int my_write(void* ctx, uint8_t port, uint8_t value) {
    auto* hw = static_cast<MyBusContext*>(ctx);
    switch (port) {
        case 0: *hw->uart_dr = value; return 0;   // stdout equivalent
        case 7: *hw->spi_dr  = value; return 0;   // telemetry channel
        default: return -1;                         // IO_UNAVAILABLE
    }
}

static int my_read(void* ctx, uint8_t port, uint8_t* value) {
    auto* hw = static_cast<MyBusContext*>(ctx);
    switch (port) {
        case 0: *value = *hw->uart_dr; return 0;
        case 3: *value = *hw->spi_dr;  return 0;
        default: return -1;
    }
}

// Usage:
static MyBusContext hw_ctx = { UART0_DR, SPI1_DR };
VMConfig cfg = {
    .instruction_limit = 100000u,
    .ports = { &hw_ctx, my_write, my_read }
};
vm_init(vm, code, code_len, nullptr, 0u, entry, cfg);
```

### Host stdio Bus

The host stdio bus is provided for development and ground simulation:

```cpp
#include "io.h"

PortBus bus = tenuis_stdio_bus();
```

Behaviour:
- Port 0 write: calls `write(1, &byte, 1)` (stdout). Always returns 0.
- Port 0 read: calls `read(0, &byte, 1)` (stdin). Returns 0 on success, -2 on EOF, -3 on error.
- Any other port: returns -1 (`IO_UNAVAILABLE`).

## Hybrid Deployment

The hybrid binary (`main_hybrid.cpp`) supports two concurrent program sources:

1. A safe-mode program baked in at compile time (Mode B — embedded C array).
2. An uplinkable program loaded from a file path at runtime (Mode A — file I/O).

At startup, the hybrid binary attempts to load the file path given as `argv[1]`. If the file is absent, the CRC check fails, or decompression fails, it falls back to the baked-in program without interrupting execution. The fallback message goes to stderr only.

This gives the deployment the fault tolerance of Mode B (the satellite always has a program to run) with the flexibility of Mode A (the program can be updated from the ground).

See `src/vm/main_hybrid.cpp` for the implementation. The CMake helper `tenuis_add_hybrid(target ten_source)` builds a hybrid binary from a `.ten` source file.

## Integration Checklist

Before deploying Tenuis on a spacecraft subsystem:

1. Implement a `PortBus` with handlers for every hardware port the program will use. Unhandled ports must return -1, not 0.
2. Set `instruction_limit` to a value that exceeds the worst-case execution path of your program with margin. Use ground simulation to measure actual instruction counts.
3. Handle all non-zero `HaltReason` values in your fault handler. Log `instructions_executed` for post-flight analysis.
4. Cross-compile `tenuisr` or the packed/hybrid binary with your target toolchain. Verify the size check still passes (`cmake/check_size.cmake` runs automatically).
5. Test the full uplink path on a hardware-in-the-loop testbed before flight. Verify that a corrupted .tenb causes clean fallback to safe-mode, not a hang.
