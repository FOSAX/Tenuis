# Tenuis

![Maintained](https://img.shields.io/badge/maintained-yes-brightgreen.svg)
![C++20](https://img.shields.io/badge/language-C%2B%2B20-blue.svg)
![License](https://img.shields.io/badge/license-BSD%202--Clause-blue.svg)
![Version](https://img.shields.io/badge/version-0.1.0-green.svg)

**Space-first programming language and runtime for ultra-constrained autonomous systems.**

> The name comes from the Latin *tenuis* — thin, slender. It describes exactly what this runtime is designed to be.

Tenuis is a stack-based bytecode language designed for environments where every byte matters: deep-space probes, CubeSats, and long-duration autonomous missions. The entire runtime — interpreter + decompressor — fits in under **10,240 bytes** with zero external dependencies.

*Voyager 1 carries 69 KB of total memory. Apollo had 4 KB of RAM (plus 72 KB ROM). Tenuis fits in what they had left over.*

## Contents

- [Motivation](#motivation)
- [At a glance](#at-a-glance)
- [The language](#the-language)
- [Instruction set](#instruction-set)
- [Toolchain](#toolchain)
- [Binary format (.tenb)](#binary-format-tenb)
- [Compression (TCF)](#compression-tcf)
- [Deployment](#deployment)
- [Installation](#installation)
- [Building](#building)
- [Testing](#testing)
- [Space Profile](#space-profile)
- [Architecture](#architecture)
- [Size budget](#size-budget)
- [Non-goals](#non-goals)
- [License](#license)

## Motivation

Modern embedded runtimes (MicroPython, Lua, Wasm) target microcontrollers with megabytes of flash. Deep-space systems have kilobytes. The constraints are real:

| System | Total RAM | Available for programs |
|--------|-----------|----------------------|
| Voyager 1 (1977) | 69 KB | ~20 KB |
| Apollo Guidance Computer | 4 KB RAM + 72 KB ROM | small fraction |
| Modern 1U CubeSat MCU | 256 KB flash | 10–50 KB after RTOS |

Tenuis occupies the gap: a complete language + runtime that survives in this environment while still being programmable from a ground station.

## At a glance

- **Stack-based VM** — 32-bit signed integer cells, separate data and return stacks
- **46 opcodes** — 24 have single-character source forms for maximum density
- **LZ77 + Huffman compression** — 2–4× compression of typical bytecode
- **TCF format** — fixed 264-byte Huffman table header, MSB-first bit stream
- **Three deployment modes** — (A) separate runtime + `.tenb` file, (B) fully self-contained binary with program embedded at compile time, or (hybrid) baked-in safe-mode with runtime uplink override
- **16-bit address space** — up to 64 KB code, 64 KB data
- **Runtime: < 10,240 bytes** — hard size budget enforced by CI
- **Zero dependencies** — no OS, no libc beyond `read`/`write`, no STL in runtime

## The language

Tenuis source files (`.ten`) are plain text. The grammar is deliberately minimal.

### Syntax

| Construct | Syntax | Example | Effect |
|-----------|--------|---------|--------|
| Integer literal | `[-]digits` | `42`, `-7` | Push value onto stack |
| Single-char op | any listed op character | `+` | Execute opcode |
| Label definition | `:name` | `:loop` | Mark current address (no bytes emitted) |
| Unconditional jump | `#name` | `#loop` | Jump to label |
| Conditional jump | `?name` | `?done` | Jump if top-of-stack is zero; pops TOS |
| Subroutine call | `(name)` | `(sum)` | Call subroutine at label |
| Line comment | `// ...` | `// note` | Ignored |

Labels can be defined **before** or **after** the instructions that reference them (forward references are backpatched).

### Hello world

```
// emit 'H' 'i' '\n', then halt
72 . 105 . 10 . _
```

### Arithmetic example

```
// (3 + 4) * 2 - 1 = 13, emit result byte
3 4 + 2 * 1 - .
_
```

### Loop example

```
// emit 'A' through 'E'
65
:loop
  $ .        // dup top, emit it
  1 +        // increment
  $ 70 <     // still below 'F'?
  ?end
  #loop
:end
, _          // drop, halt
```

### Subroutine example

```
// sum_to(5) = 15, emit result
5 (sum_to) .
_

:sum_to        // ( n -- n*(n+1)/2 )
  $ 1 +
  *
  2 /
  ;
```

## Instruction set

### Stack manipulation

| Hex | Mnemonic | Source | Stack effect | Description |
|-----|----------|--------|--------------|-------------|
| 00 | NOP | — | ( -- ) | No operation |
| 01 | HALT | `_` | ( -- ) | Stop execution, exit OK |
| 02 | PUSH8 | integer | ( -- n ) | Push sign-extended 8-bit immediate |
| 03 | PUSH16 | integer | ( -- n ) | Push sign-extended 16-bit LE immediate |
| 04 | PUSH32 | integer | ( -- n ) | Push 32-bit LE immediate |
| 05 | DROP | `,` | ( a -- ) | Discard top of stack |
| 06 | DUP | `$` | ( a -- a a ) | Duplicate top of stack |
| 07 | OVER | `"` | ( a b -- a b a ) | Copy second item to top |
| 08 | SWAP | `'` | ( a b -- b a ) | Exchange top two items |

### Arithmetic

| Hex | Mnemonic | Source | Stack effect | Description |
|-----|----------|--------|--------------|-------------|
| 10 | ADD | `+` | ( a b -- a+b ) | |
| 11 | SUB | `-` | ( a b -- a-b ) | |
| 12 | MUL | `*` | ( a b -- a*b ) | |
| 13 | DIV | `/` | ( a b -- a/b ) | Signed; halts on divide-by-zero |
| 14 | MOD | `%` | ( a b -- a%b ) | Signed remainder; halts on divide-by-zero |

### Bitwise

| Hex | Mnemonic | Source | Stack effect | Description |
|-----|----------|--------|--------------|-------------|
| 18 | AND | `&` | ( a b -- a&b ) | Bitwise AND |
| 19 | OR | `\|` | ( a b -- a\|b ) | Bitwise OR |
| 1A | XOR | `^` | ( a b -- a^b ) | Bitwise XOR |
| 1B | NOT | `~` | ( a -- ~a ) | Bitwise NOT (one's complement) |
| 1C | SHL | `[` | ( a b -- a<<b ) | Left shift |
| 1D | SHR | `]` | ( a b -- a>>b ) | Arithmetic right shift |

### Comparison

| Hex | Mnemonic | Source | Stack effect | Description |
|-----|----------|--------|--------------|-------------|
| 20 | EQ | `=` | ( a b -- a==b ) | 1 if equal, 0 otherwise |
| 22 | LT | `<` | ( a b -- a<b ) | 1 if a < b (signed), 0 otherwise |
| 23 | GT | `>` | ( a b -- a>b ) | 1 if a > b (signed), 0 otherwise |

### Memory

| Hex | Mnemonic | Source | Stack effect | Description |
|-----|----------|--------|--------------|-------------|
| 30 | LOAD32 | `@` | ( addr -- val ) | Load 32-bit LE word from data memory |
| 38 | STORE32 | `!` | ( val addr -- ) | Store 32-bit LE word to data memory |

### Control flow

| Hex | Mnemonic | Source | Stack effect | Description |
|-----|----------|--------|--------------|-------------|
| 40 | JMP | `#name` | ( -- ) | Unconditional jump to 16-bit LE address |
| 41 | JZ | `?name` | ( a -- ) | Jump if TOS == 0; always pops TOS |
| 43 | CALL | `(name)` | ( -- ) | Push return address, jump to 16-bit LE address |
| 44 | RET | `;` | ( -- ) | Pop return address, jump to it |

### I/O

| Hex | Mnemonic | Source | Stack effect | Description |
|-----|----------|--------|--------------|-------------|
| 50 | EMIT | `.` | ( b -- ) | Write low byte of TOS to port 0 |
| 51 | READ | `` ` `` | ( -- b ) | Read one byte from port 0 (0–255) |
| 52 | WRITE_PORT | `W<n>` | ( b -- ) | Write low byte of TOS to port n (0–255) |
| 53 | READ_PORT | `R<n>` | ( -- b ) | Read one byte from port n (0–255) |

**Integer encoding** — the compiler chooses the smallest representation automatically:
- Value fits in `[-128, 127]` → PUSH8 (2 bytes)
- Value fits in `[-32768, 32767]` → PUSH16 (3 bytes)
- Otherwise → PUSH32 (5 bytes)

## Toolchain

### `tenuisc` — compiler

Compiles a `.ten` source file to a `.tenb` binary. Applies LZ77+Huffman compression automatically when it reduces size; falls back to uncompressed otherwise.

```
tenuisc <input.ten> <output.tenb>
```

```
$ tenuisc program.ten program.tenb
tenuisc: program.ten → program.tenb  (769 → 464 bytes, 60% of original)
```

### `tenuisr` — runtime interpreter

Loads and executes a `.tenb` binary.

```
tenuisr <program.tenb>
```

Handles both compressed (`F_COMPRESSED=1`) and uncompressed binaries transparently.

### `tenuispack` — binary embedder

Converts a `.tenb` file into a C++ source file (`TENUIS_PROGRAM[]` array) for zero-file deployment.

```
tenuispack <input.tenb> <output.cpp>
```

The generated `.cpp` is compiled together with `src/vm/main_embedded.cpp` and the VM sources to produce a standalone executable that carries its own program — no external `.tenb` required at runtime.

### `mktenb` — raw bytecode wrapper

Host tool for testing: wraps arbitrary raw bytecode bytes into a valid `.tenb` header.

```
mktenb output.tenb 02 05 50 01
mktenb output.tenb 02 05 50 01 --data ff 00
```

### `tcf_roundtrip` — compression tester

Verifies that `compress → decompress` produces the original bytes exactly.

```
tcf_roundtrip <binary-file>
```

## Binary format (.tenb)

Every Tenuis binary begins with a **20-byte header**:

```
Offset  Size  Field
──────  ────  ─────────────────────────────────────────
0       3     Magic: 0x54 0x45 0x4E  ('T' 'E' 'N')
3       1     Format version (copy of byte 4)
4       1     Format version: 1
5       1     Flags (see below)
6       2     Code size uncompressed, uint16 LE
8       2     Code size stored, uint16 LE
10      2     Data segment size, uint16 LE
12      2     Entry point offset, uint16 LE
14      2     Header CRC-16/CCITT-FALSE (covers bytes 0–13)
16      4     Payload CRC-32/ISO-HDLC (covers bytes 20…end)
20      cs    Code segment (cs = code size stored)
20+cs   ds    Data segment (ds = data segment size)
```

**Flag bits (byte 5):**

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | `F_COMPRESSED` | Code segment is a TCF compressed stream |
| 1 | `F_HAS_DATA` | Data segment is present |
| 2 | `F_LITTLE_END` | Endianness marker (always 1 in version 1) |

The loader performs 12 integrity checks before execution: magic, version, header CRC-16, size bounds, entry point, payload CRC-32, and decompression success. See `docs/BYTECODE_FORMAT.md` for the complete verification sequence.

## Compression (TCF)

Tenuis Compression Format v1 applies two stages to the raw bytecode:

**Stage 1 — LZ77** (window 4 096 bytes, min match 3, max match 258, lazy matching)

Back-references in the compressed stream use `(offset–1)` encoded in 12 bits and `(length–3)` in 8 bits. The sliding window is the output buffer itself — no separate allocation.

**Stage 2 — Canonical Huffman** (max code length 15 bits, MSB-first)

A 258-symbol alphabet: 0–255 = literal bytes, 256 = END-of-stream, 257 = MATCH token. Codes are assigned canonically (sorted by length then symbol value) so the decoder needs only the 258 code-length bytes — no tree structure.

**TCF stream layout:**

```
Offset  Size   Field
──────  ─────  ─────────────────────────────────
0       4      Magic: 'T' 'C' 'F' '1'
4       2      Number of symbols: 258 (uint16 LE)
6       258    Code lengths[0..257] (uint8 each; 0 = unused)
264     …      Huffman-coded bit stream (MSB-first)
```

The 264-byte fixed overhead means compression only helps for programs larger than roughly 300 bytes. Smaller programs are stored uncompressed (`F_COMPRESSED=0`) automatically.

**Typical compression ratios:**

| Program size | Expected ratio |
|-------------|---------------|
| < 300 B | < 1× (stored uncompressed) |
| 300 B – 1 KB | 1.3 – 1.8× |
| 1 – 8 KB | 2 – 4× |

See `docs/COMPRESSION_SPEC.md` for the full normative specification.

## Deployment

### Mode A — separate runtime + program file

Ship `tenuisr` to the target alongside the `.tenb` program file. The runtime is ~8 KB; programs compress to a fraction of their raw size.

```
# Ground station:
tenuisc mission.ten mission.tenb           # compile + compress
# Transfer mission.tenb to spacecraft storage

# On-board:
tenuisr mission.tenb                       # load, decompress, execute
```

### Mode B — self-contained packed binary

Embed program and runtime into a single binary at build time. No file system access at all — the program is baked into `.rodata`.

```
# Ground station:
tenuisc mission.ten mission.tenb
tenuispack mission.tenb mission_program.cpp

# Build standalone executable (or use CMake helper):
g++ -Os -fno-exceptions -fno-rtti -I src/vm \
    src/vm/main_embedded.cpp mission_program.cpp \
    src/vm/{loader,vm,io,crc,decompress}.cpp \
    -o mission_standalone

# Deploy: single binary, zero file I/O
./mission_standalone
```

**CMake helper** (defined in `CMakeLists.txt`):

```cmake
tenuis_add_packed(my_mission tests/fixtures/triangles.ten)
```

This creates target `my_mission`: compiles, compresses, embeds, and links in one step.

### Mode hybrid — baked-in safe-mode with uplink override

The hybrid binary carries a default program compiled in at build time (like Mode B), but can load and execute a different `.tenb` from a file path at runtime (like Mode A). If the uplink file is absent, corrupted, or fails CRC validation, execution falls back silently to the baked-in program.

```
# Build the hybrid binary with safe_mode.ten as the default:
tenuis_add_hybrid(my_hybrid tests/fixtures/safe_mode.ten)

# On-board — no uplink file: runs baked-in safe-mode program
./my_hybrid

# On-board — with uplink file: runs the uploaded program
./my_hybrid /storage/uplinked_mission.tenb

# If the uplink file is corrupt, the safe-mode program runs instead
```

**CMake helper:**

```cmake
tenuis_add_hybrid(my_hybrid path/to/safe_mode.ten)
```

## Installation

### Pre-built binaries

Download the archive for your platform from the [Releases page](../../releases):

| Platform | Archive |
|----------|---------|
| Linux x86_64 | `tenuis-linux-x86_64.tar.gz` |
| macOS Apple Silicon | `tenuis-macos-arm64.tar.gz` |
| macOS Intel | use the arm64 archive via Rosetta 2 |

```bash
tar xzf tenuis-linux-x86_64.tar.gz
cd tenuis-linux-x86_64

./tenuisc program.ten program.tenb
./tenuisr program.tenb
```

The archive contains three executables: `tenuisc` (compiler), `tenuisr` (runtime), `tenuispack` (binary embedder).

Windows is not natively supported. Use WSL with the Linux archive.

## Building

**Requirements:** CMake ≥ 3.16, C++20 compiler (GCC or Clang), `xxd` (for tests).

```bash
# Clone the repository
git clone <repository-url>
cd Tenuis

# Configure + build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# The size check runs automatically at build time:
# -- Interpreter size: 9548 B  (budget 10240 B, headroom 692 B)  OK
```

**Build outputs:**

| Binary | Location | Description |
|--------|----------|-------------|
| `tenuisr` | `build/tenuisr` | Runtime interpreter |
| `tenuisc` | `build/tenuisc` | Compiler + compressor |
| `tenuispack` | `build/tenuispack` | Binary embedder |
| `mktenb` | `build/mktenb` | Raw bytecode wrapper (testing) |
| `tcf_roundtrip` | `build/tcf_roundtrip` | Compression round-trip tester |
| `*_packed` | `build/*_packed` | Standalone packed binaries for each fixture |

## Testing

```bash
ctest --test-dir build --output-on-failure
```

**Six test suites:**

| Suite | What it tests |
|-------|--------------|
| `phase1_tests` | VM opcodes via raw `.tenb` files (mktenb path) |
| `phase2_tests` | Compiler: source → bytecode → correct output |
| `phase3_tests` | Compression: TCF round-trip, F_COMPRESSED=1 execution, regression |
| `phase4_tests` | Packed binaries: correct output, no `open()` syscall |
| `phase5_tests` | Budget enforcement, PortBus dispatch, `R<n>`/`W<n>` opcodes |
| `phase6_tests` | Hybrid mode: embedded fallback, uplink override, corrupted uplink rejection |

All suites must pass before any merge. The size budget check (`≤ 10,240 B`) runs as a CMake POST_BUILD step and breaks the build if exceeded.

## Space Profile

The Space Profile adds two deterministic safety features required for autonomous operation.

### Instruction budget

Limits total instructions executed per run — prevents runaway loops caused by bit-flip corruption or unexpected inputs.

```bash
tenuisr -b 50000 program.tenb
```

If the budget is reached before `HALT`, the VM stops immediately with `HaltReason::BUDGET_EXCEEDED` (exit code 2). `-b 0` (default) is unlimited.

Every run returns a `VMResult` containing:

```cpp
struct VMResult {
    HaltReason reason;              // OK, BUDGET_EXCEEDED, IO_UNAVAILABLE, ...
    uint32_t   instructions_executed;  // exact count, usable for telemetry
};
```

### Port bus (I/O abstraction)

I/O is routed through a `PortBus` — a pair of function pointers swappable per target. On the host, port 0 maps to stdin/stdout. On a spacecraft, the integrator installs a bus that routes to the actual peripherals (UART, SPI, sensors).

**Source syntax:**

| Syntax | Opcode | Effect |
|--------|--------|--------|
| `.` | `0x50` | Write TOS low byte to **port 0** |
| `` ` `` | `0x51` | Read one byte from **port 0**, push |
| `W<n>` | `0x52 <n>` | Write TOS low byte to **port n** |
| `R<n>` | `0x53 <n>` | Read one byte from **port n**, push |

Port numbers are 0–255 (checked at compile time). Writing to a port with no handler yields `HaltReason::IO_UNAVAILABLE`.

**Example — read from a sensor on port 3, write result to telemetry port 7:**

```
R3 W7 _
```

**Implementing a custom bus:**

```cpp
static int my_write(void* ctx, uint8_t port, uint8_t value) {
    if (port == 7) { telemetry_send(value); return 0; }
    return -1;  // IO_UNAVAILABLE for unhandled ports
}
static int my_read(void* ctx, uint8_t port, uint8_t* value) {
    if (port == 3) { *value = sensor_read(); return 0; }
    return -1;
}
// Pass to vm_init via VMConfig:
VMConfig cfg = { .instruction_limit = 50000, .ports = { nullptr, my_write, my_read } };
vm_init(vm, code, code_len, nullptr, 0, entry, cfg);
```

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  Ground-station toolchain (host, no size constraint)    │
│                                                         │
│  source.ten  ──[tenuisc]──►  source.tenb               │
│                  │                │                     │
│              compress.cpp      .tenb ──[tenuispack]──►  │
│              (LZ77+Huffman)        source_embedded.cpp  │
└─────────────────────────────────────────────────────────┘
                    │ upload
┌───────────────────▼─────────────────────────────────────┐
│  On-board runtime  (< 10,240 B .text+.rodata)           │
│                                                         │
│  main.cpp ──► loader.cpp ──► decompress.cpp             │
│                                   │                     │
│                              vm.cpp (46 opcodes)        │
│                              io.cpp (EMIT / READ)       │
│                              crc.cpp (CRC-16, CRC-32)   │
└─────────────────────────────────────────────────────────┘
```

**Memory layout at runtime** (defaults, all overridable via `-D` flags):

| Region | Default size | Purpose |
|--------|-------------|---------|
| Code buffer | 8 192 B | Decompressed bytecode |
| Data memory | 4 096 B | `@` / `!` read-write heap |
| Data stack | 64 cells × 4 B = 256 B | Operand stack |
| Return stack | 32 cells × 2 B = 64 B | Call/return addresses |
| **Total** | **12 608 B** | |

Adjust for your target with:
```cmake
target_compile_definitions(tenuisr PRIVATE
    TENUIS_CODE_SIZE=4096
    TENUIS_DATA_SIZE=2048
    TENUIS_STACK_DEPTH=32
    TENUIS_RSTACK_DEPTH=16
)
```

See `docs/ARCHITECTURE.md` for the full module breakdown and data-flow diagram.

## Size budget

The interpreter (`tenuisr`) must stay within **10,240 bytes** of `.text + .rodata`. This is enforced on every build via `cmake/check_size.cmake`.

Current allocation:

| Component | Budget | Actual |
|-----------|--------|--------|
| VM dispatch loop | 2 112 B | |
| Loader + CRC | 896 B | |
| Decompressor | 2 048 B | |
| I/O | 128 B | |
| Startup + misc | 512 B | |
| **Total** | **10,240 B** | **~9,548 B** |
| Headroom | — | **~692 B** |

The size check script is `cmake/check_size.cmake`. It parses `size` output and fails the build immediately if the budget is exceeded.

## Non-goals

Tenuis explicitly does not aim to:

- **Replace general-purpose embedded languages** — if you have 1 MB of flash, use MicroPython.
- **Provide floating-point arithmetic** — space systems compute in fixed-point; float support would bloat the runtime past the budget.
- **Support concurrency or interrupts** — the runtime is single-threaded; interrupt handling belongs to the surrounding RTOS.
- **Provide a standard library** — host-side code generates pre-computed tables; the VM executes them.
- **Be Turing-complete in a useful sense on minimal hardware** — practical programs are bounded by the 16-bit address space and fixed memory.

## License

Tenuis is released under the BSD 2-Clause License. You can use, modify, and distribute it freely in both open source and proprietary projects, provided the copyright notice is retained.

See [LICENSE](LICENSE) for the full text.

SPDX-License-Identifier: `BSD-2-Clause`
