# Tenuis Interpreter Size Budget

Version: 1.0  
Status: Normative

## Overview

The Tenuis interpreter (everything that runs on the target system) must fit within **10 240 bytes** (10 kB) of compiled code and read-only data (`.text` + `.rodata`). This is a hard budget enforced by the CI pipeline on every commit. It is not a target to approach asymptotically — it is a ceiling that must never be breached.

The budget covers only the **runtime interpreter** (`tenuisr`): the binary loader, decompressor, VM loop, and I/O shim. The host toolchain (`tenuisc`) has no size constraint.

RAM usage (stack, data memory, code buffer) is tracked separately as a **RAM footprint** and is not subject to the 10 kB limit, but default values must fit within Voyager-class constraints (see below).

## Measurement Methodology

### What Is Measured

The compiler is invoked with flags that produce the most aggressively stripped binary:

```sh
c++ -std=c++20 -Os -ffunction-sections -fdata-sections \
    -fno-exceptions -fno-rtti \
    -Wl,--gc-sections \
    -o tenuisr src/vm/*.cpp
strip --strip-all tenuisr
size tenuisr
```

The measured value is the **sum of the `text` and `data` columns** from `size` (not `bss`, which is runtime RAM). On ELF targets, this equals `sizeof(.text) + sizeof(.rodata) + sizeof(.data)`.

On bare-metal targets (no ELF), the measure is the size of the compiled `.text` section output by the linker map.

The CI script asserts:

```sh
BINARY_SIZE=$(size tenuisr | awk 'NR==2 { print $1 + $2 }')
if [ "$BINARY_SIZE" -gt 10240 ]; then
  echo "FAIL: interpreter size ${BINARY_SIZE} B exceeds 10 240 B budget"
  exit 1
fi
```

### Compiler and Flags

The canonical measurement uses **GCC 12+** with **`-Os`** (optimise for size). LLVM/Clang with `-Oz` may produce different sizes; the CI uses GCC as the reference. Implementors using Clang should verify the result is within budget on GCC before merging.

### Target Architecture

The primary measurement is on **x86-64 Linux** (the common CI host). Because x86-64 instructions are generally larger than ARM Thumb-2 or RISC-V compressed, staying under 10 kB on x86-64 strongly implies staying under budget on the embedded targets Tenuis actually runs on.

For embedded targets, the CI also runs a secondary measurement using:
- **ARM Cortex-M0 (ARMv6-M, Thumb-only)**: typically produces binaries 30–50% smaller than x86-64.
- A toolchain cross-compiled with `arm-none-eabi-gcc -mcpu=cortex-m0 -mthumb -Os`.

## Component Budget Table

The total budget of 10 240 B is divided among components. Each component has an **allocated budget** and a **current measured size** (filled in during implementation). The sum of allocated budgets is 9 800 B, leaving a **440-byte reserve** for unforeseeable growth.

| Component | Source file(s) | Allocated (B) | Measured (B) | Notes |
|---|---|---:|---:|---|
| Entry point & startup | `src/vm/main.cpp` | 256 | TBD | `main()`, argument parsing, file open/mmap |
| Binary loader | `src/vm/loader.cpp` | 768 | TBD | Header read, version/magic check, entry-point validation |
| CRC-16 verifier | `src/vm/crc.cpp` | 256 | TBD | CRC-16/CCITT-FALSE; table-driven (32-byte table) |
| CRC-32 verifier | `src/vm/crc.cpp` | 256 | TBD | CRC-32/ISO-HDLC; table-driven (256-byte table, shared with common CRC implementations) |
| Huffman decoder | `src/vm/decompress.cpp` | 768 | TBD | Table reconstruction from code-lengths + decode loop |
| LZ77 decoder | `src/vm/decompress.cpp` | 512 | TBD | MATCH expand + literal copy; shares output buffer as window |
| Bit reader | `src/vm/decompress.cpp` | 256 | TBD | 32-bit accumulator, byte-refill loop |
| VM dispatch loop | `src/vm/vm.cpp` | 512 | TBD | Main `while(running)` + switch, PC management |
| Stack operations (NOP, HALT, PUSH*, POP, DUP, SWAP, OVER, ROT) | `src/vm/vm.cpp` | 384 | TBD | |
| Arithmetic ops (ADD–DEC, 8 opcodes) | `src/vm/vm.cpp` | 384 | TBD | |
| Bitwise ops (AND–SAR, 7 opcodes) | `src/vm/vm.cpp` | 256 | TBD | |
| Comparison ops (EQ–GE, 6 opcodes) | `src/vm/vm.cpp` | 192 | TBD | |
| Memory ops (LOAD*, STORE*, 6 opcodes) | `src/vm/vm.cpp` | 384 | TBD | Bounds checks included |
| Control flow ops (JMP, JZ, JNZ, CALL, RET) | `src/vm/vm.cpp` | 384 | TBD | Return stack management |
| I/O ops (EMIT, READ) | `src/vm/vm.cpp` + `src/vm/io.cpp` | 384 | TBD | Calls platform shim |
| I/O shim (POSIX) | `src/vm/io_posix.cpp` | 256 | TBD | `write(1,…)` / `read(0,…)` |
| I/O shim (bare-metal stub) | `src/vm/io_baremetal.cpp` | 128 | TBD | Weak-linked placeholder; replaced by platform |
| Error diagnostics | `src/vm/error.cpp` | 384 | TBD | Error strings + halt path |
| **Subtotal** | | **6 720** | TBD | |
| **Spare / unallocated** | | **3 080** | — | Cushion before hitting 10 240 B |

> **Note on spare**: the 3 080 B of unallocated budget is intentional. In practice, the compiler does not emit separate object code for every opcode case within a switch statement — many cases share instruction sequences, and the linker packs them together. The component breakdown above treats each category as independent; the actual total will be significantly smaller. The spare absorbs compiler variability across targets and toolchain versions.

## RAM Footprint Budget

RAM usage is separate from the code-size budget but is documented here for completeness. These are the **static** allocations (stack segment, `.bss`):

| Region | Constant | Default (B) | Minimum (B) | Notes |
|---|---|---:|---:|---|
| Code buffer | `TENUIS_CODE_SIZE` | 8 192 | 512 | Must hold the largest program the target will run |
| Data memory | `TENUIS_DATA_SIZE` | 4 096 | 64 | Must hold all data the program accesses |
| Value stack | `TENUIS_STACK_DEPTH × 4` | 256 (64 entries) | 64 (16 entries) | Depth of 16 is sufficient for simple sequential programs |
| Return stack | `TENUIS_RSTACK_DEPTH × 2` | 64 (32 entries) | 16 (8 entries) | Depth of 8 allows 8 levels of subroutine nesting |
| Decomp scratch | none (uses code buffer) | 0 | 0 | Decompressor writes directly into code buffer |
| Huffman table | 258 B (static array in decomp) | 258 | 258 | Fixed regardless of program |
| **Total default** | | **12 866** | — | ≈ 12.6 kB |
| **Total minimum** | | — | **1 000** | 512 + 64 + 64 + 16 + 0 + 258 = 914 B, round up |

### Voyager-Class Sizing Example

Voyager 1 has 69 kB total memory. Allocating Tenuis conservatively:

| Use | Size |
|---|---|
| Tenuis interpreter code | 10 kB |
| Code buffer | 8 kB |
| Data memory | 4 kB |
| Stacks + table | 0.6 kB |
| **Tenuis total** | **22.6 kB** |
| Other flight software | **≤ 46.4 kB** |

This is a reasonable allocation: Tenuis occupies about one-third of Voyager's memory while leaving the remaining two-thirds for system software, telemetry buffers, and sensor interfaces.

### CubeSat-Class Sizing Example

A modern CubeSat with 16 MB RAM:

| Use | Size |
|---|---|
| Tenuis interpreter code | 10 kB |
| Code buffer (full) | 64 kB (`TENUIS_CODE_SIZE = 65535`) |
| Data memory (full) | 64 kB (`TENUIS_DATA_SIZE = 65535`) |
| Stacks + table | 0.6 kB |
| **Tenuis total** | **138.6 kB** |
| Other flight software | **~15.9 MB** |

Tenuis occupies less than 1% of available CubeSat memory.

## CI Enforcement

The size budget is enforced by the CI workflow at `.github/workflows/ci.yml` (added in Phase 5). The size check runs on every pull request and every push to `main`.

The check must:
1. Build with the canonical flags (see Measurement Methodology above).
2. Extract the binary size using `size`.
3. Compare against 10 240.
4. Fail the CI job (non-zero exit code) if the budget is exceeded.
5. Print the current size alongside the budget in the CI log for every run, whether passing or failing.

Example output on pass:
```
Interpreter size: 7 814 B (budget: 10 240 B, headroom: 2 426 B)  ✓
```

Example output on fail:
```
Interpreter size: 10 417 B (budget: 10 240 B, overage: 177 B)  ✗
FAIL: size budget exceeded
```

## Component Ownership and Measurement

Each component corresponds to one or more source files. When a component's measured size first becomes available (Phase 1 and later), update the "Measured (B)" column in the table above. If a component's measured size approaches its allocated budget, raise it in the PR that triggered the growth — do not silently absorb the difference from the spare.

### How to Measure a Single Component

```sh
# Build with debugging symbols for size breakdown
c++ -std=c++20 -Os -ffunction-sections -c src/vm/decompress.cpp -o /tmp/decompress.o
size /tmp/decompress.o
# Sum text + data columns
```

For a function-level breakdown:
```sh
nm --print-size --size-sort /tmp/decompress.o | grep ' T \| t '
```

## Design Principles for Staying Within Budget

These are the rules that, if followed, keep the interpreter small:

1. **No STL containers** — `std::vector`, `std::map`, etc. pull in allocation code. Use raw arrays.
2. **No exceptions** — `-fno-exceptions` eliminates exception table overhead (can save 1–3 kB on GCC).
3. **No RTTI** — `-fno-rtti` eliminates type-info tables.
4. **No virtual functions** — vtable and vtable-dispatch overhead. Use function pointers or a switch if dispatch is needed.
5. **Minimise string literals** — every error message string is `.rodata` that counts toward the budget. Keep messages short.
6. **Prefer `uint8_t` arrays over structs with padding** — struct padding is invisible but wastes space and increases binary size when accessed with generated alignment code.
7. **Opcode dispatch via switch, not function pointers** — a switch on a dense enum allows the compiler to emit a jump table; function-pointer arrays require 8 bytes per entry on 64-bit targets (37 opcodes × 8 B = 296 B of table alone).
8. **Inline the CRC table at compile time** — use `constexpr` to generate the CRC table so it lives in `.rodata` (read-only flash) rather than being recomputed at startup.
