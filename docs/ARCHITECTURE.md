# Tenuis Architecture

## Overview

Tenuis is a space-first programming language and runtime designed around one constraint: maximum information density per byte of onboard storage. It targets environments where memory is the hardest limit — deep-space probes, CubeSats, long-duration autonomous systems with no update path — and where a complete program must fit in kilobytes, not megabytes.

The name comes from the Latin *tenuis* (thin, slender). The design goal is minimum footprint, not speed and not ergonomics.

Reference constraints that motivate the design:

| Target system | Total onboard memory |
|---|---|
| Apollo Guidance Computer (1969) | 4 KB |
| Voyager 1 (1977) | 69 KB |
| Modern hardened CubeSat (current) | ≤ 16 MB |
| Mars rover compute (current) | ~128 MB |

Tenuis is designed so that a complete program — source compiled to bytecode, compressed, bundled with its own decompressor and VM — fits within 10–64 KB of deployable storage.

## Hard Constraints

These are budgets enforced by CI, not aspirations:

| Constraint | Value | Enforcement |
|---|---|---|
| Interpreter compiled size (.text + .rodata) | < 10 240 B (10 kB) | CI size check |
| External dependencies at runtime | Zero | Linker check (no libc symbols except `memcpy`, `memset`) |
| Dynamic allocation in VM hot path | Prohibited | Code review |
| C++ STL containers in interpreter | Prohibited | Code review |
| Target architecture range | 8-bit to 64-bit | Compile matrix |
| OS assumption in interpreter | None | Bare-metal build target |

## System Modules

Tenuis is split into two sharply separated concerns:

- **Host toolchain** — runs on a developer workstation; has no size constraint; transforms source into a deployable binary.
- **Target runtime** — runs on the deployed system; has a hard 10 kB compiled-size budget; executes the bytecode.

### Host Toolchain (`tenuisc`)

A single host binary comprising four sequential stages:

```
Source (.ten)
    │
    ▼
[1. Lexer]
    │ token stream
    ▼
[2. Parser / Code Generator]
    │ raw bytecode (uncompressed)
    ▼
[3. Compressor  — LZ77 + Huffman]
    │ compressed bytecode
    ▼
[4. Packager]
    │
    ▼
Tenuis binary (.tenb)
```

**1. Lexer** — tokenises source. Recognises integer literals, single-character operation tokens, label definitions (`:name`), label references (`#name`, `?name`), call syntax (`(name)`), and line comments (`//`). Produces a flat token stream.

**2. Parser / Code Generator** — single-pass with a forward-reference patch table. Emits raw bytecode per `INSTRUCTION_SET.md`. Selects the shortest `PUSH8`/`PUSH16`/`PUSH32` encoding for each integer literal. Resolves label addresses by backpatching after the first pass.

**3. Compressor** — applies LZ77 followed by Huffman coding to the complete raw bytecode segment, producing a Tenuis Compression Format (TCF) byte stream. Described in full in `COMPRESSION_SPEC.md`. This pass is unconditional: the toolchain always compresses; no "store raw" output mode exists. (The decompressor is always present in the runtime.)

**4. Packager** — writes the final `.tenb` binary: a fixed 20-byte header, the compressed code segment, and the data segment. Optionally emits a C header (`-embed` flag) that wraps the `.tenb` as a `const uint8_t PROGRAM[]` for link-time embedding. Described in `BYTECODE_FORMAT.md`.

### Target Runtime (`tenuisr` or embedded `vm.cpp`)

A single C++20 translation unit with four responsibilities:

**1. Binary Loader** — reads and validates the `.tenb` header: checks magic bytes, version, CRC32 of payload. Fails fast with a diagnostic if any check fails.

**2. Decompressor** — runs once on startup. Expands the compressed code segment into a statically allocated scratch buffer (`code[]`) before execution begins. Described in `COMPRESSION_SPEC.md`.

**3. VM Loop** — the main dispatch loop. Executes one opcode per iteration from the decompressed `code[]` buffer. Stack-based. No dynamic allocation. Uses only raw arrays.

**4. I/O Shim** — a pair of functions: `tenuis_emit(uint8_t)` (write one byte out) and `uint8_t tenuis_read()` (read one byte in). On POSIX: maps to `write(1, ...)` / `read(0, ...)`. On bare-metal: maps to hardware registers. This is the only platform-specific code in the runtime.

## Data Flow (End to End)

```
Developer machine                       Target system
─────────────────────────────────────   ──────────────────────────────────
source.ten
    │
    tenuisc
    ├─ lex → token stream
    ├─ parse/codegen → bytecode
    ├─ LZ77+Huffman → compressed bytes
    └─ package → program.tenb
                    │
                    ├─ (file transfer / flash write)
                    │
                    └──────────────────────────────► program.tenb in flash
                                                          │
                                                      tenuisr (VM)
                                                      ├─ load header
                                                      ├─ verify CRC32
                                                      ├─ decompress → code[]
                                                      └─ VM loop → execute
```

## Memory Layout on Target

The VM uses four statically allocated regions. All sizes are compile-time constants with the defaults shown:

| Region | Constant | Default | Description |
|---|---|---|---|
| Code buffer | `TENUIS_CODE_SIZE` | 8 192 B | Holds decompressed bytecode |
| Data memory | `TENUIS_DATA_SIZE` | 4 096 B | Read/write data segment |
| Value stack | `TENUIS_STACK_DEPTH × 4 B` | 64 × 4 = 256 B | 32-bit signed integer stack |
| Return stack | `TENUIS_RSTACK_DEPTH × 2 B` | 32 × 2 = 64 B | 16-bit return addresses |

**Total default RAM footprint**: 8 192 + 4 096 + 256 + 64 = **12 608 B** (≈ 12 kB), plus the VM code itself (~10 kB). Well within Voyager-class (69 KB) with room for other systems.

For tighter targets, reduce `TENUIS_CODE_SIZE` and `TENUIS_DATA_SIZE` at compile time.

## Deployment Model

A Tenuis binary (`.tenb`) is a self-contained program unit. It contains:

- A 20-byte header with magic identifier, version, layout sizes, and CRC32.
- The compressed bytecode payload (LZ77+Huffman).
- An optional initialised data segment (copied to RAM on startup).

There are two deployment modes:

**Mode A — Separate runtime (development and CubeSat-class targets):**
The runtime (`tenuisr`) is flashed once. Programs are loaded as `.tenb` files and passed to the runtime, either via file argument or via a known flash address.

**Mode B — Link-time embedding (Voyager-class and smaller targets):**
The toolchain's `-embed` flag emits a C header wrapping the `.tenb` as `PROGRAM[]`. This array is compiled into the runtime binary. The resulting single binary contains the VM, the decompressor, and the program. No file system required.

## Module Boundaries

| Module | Lives in | Size budget | Dependencies |
|---|---|---|---|
| Lexer | Host toolchain | Unconstrained | — |
| Parser / Codegen | Host toolchain | Unconstrained | Lexer |
| Compressor | Host toolchain | Unconstrained | — |
| Packager | Host toolchain | Unconstrained | Compressor |
| Binary Loader | Target runtime | 512 B | — |
| Decompressor | Target runtime | 2 048 B | — |
| VM Loop | Target runtime | 4 608 B | Binary Loader, Decompressor |
| I/O Shim | Target runtime | 512 B | Platform only |
| CRC32 | Target runtime | 512 B | — |

The target runtime modules form a strict DAG: Binary Loader → Decompressor → VM Loop → I/O Shim. No cycles. The decompressor is called exactly once before the VM loop starts and never again during execution.

## Explicit Non-Goals

### Not a general-purpose language

Tenuis does not provide: strings, floating-point arithmetic, object orientation, closures, exceptions, garbage collection, or any feature that costs bytecode density or interpreter size without a compelling mission justification. If you need those, use a different language.

### Not optimising for execution speed

The instruction set is designed for compressibility and decompressor simplicity, not for fast execution. Dispatch is a switch statement, not computed-goto or JIT. Programs that need high throughput belong on faster hardware.

### Not optimising for developer ergonomics

No REPL, no debugger, no IDE integration, no package manager, no syntax highlighting, no error recovery in the compiler. Source files are short because programs are short.

### Not a command/sequencing layer

Tenuis is not a replacement for, and does not compete with:

| System | What it is |
|---|---|
| CCSDS 121.0-B | Lossless telemetry data compression standard |
| CCSDS 101.0-B (SOIS) | Service-oriented onboard infrastructure standard |
| VML | Virtual Machine Language for spacecraft command sequencing |
| SCL | Spacecraft Command Language (ground-side uplink scripting) |
| F´ (F Prime) | NASA JPL C++ component framework for flight software |

Tenuis operates at the level of a compact programmable runtime. It can run *as a component within* an F´ system, or *alongside* a CCSDS telemetry stack, but it does not replace any of them. If your problem is telemetry compression, use CCSDS 121.0. If your problem is command sequencing, use VML or SCL. If your problem is "I need to store and execute 8 KB of logic on a 69 KB spacecraft", Tenuis is the right tool.

### Not a desktop executable packer

Tenuis is not a replacement for UPX or similar executable packers. Those target OS-hosted binaries with dynamic loaders. Tenuis is bare-metal-first: no OS, no loader, no assumptions about the execution environment beyond "there is a CPU and some RAM."

### No runtime dependencies

The interpreter must produce a valid freestanding binary. It may use `memcpy` and `memset` (which compilers often inline or can be provided by a tiny `lib.c`), but nothing else from libc, libm, or any OS library.

### No dynamic allocation in the VM hot path

The VM loop does not call `malloc`, `new`, `operator new`, or any allocator during program execution. All memory (code buffer, data buffer, value stack, return stack) is allocated as static arrays before execution begins.

### No C++ STL containers in the interpreter

`std::vector`, `std::map`, `std::unordered_map`, `std::string`, and all other STL containers are banned from the target runtime. Their compiled footprint is unpredictable across compilers and target architectures. Use raw arrays and manual pointer arithmetic instead.

## Comparison with Related Work

| System | Kind | Key difference from Tenuis |
|---|---|---|
| CCSDS 121.0-B | Data compression codec | Compresses *telemetry data*, not executable bytecode. No runtime. |
| VML | Spacecraft sequencing VM | Targets procedure/command sequencing, not arbitrary computation. No native compression. |
| SCL | Ground scripting language | Ground-side tool; never runs onboard. |
| F´ | Flight software framework | A framework in C++, not a language or VM. Tenuis can be an F´ component. |
| UPX | Executable packer | OS-hosted, dynamic-loader-dependent. Not bare-metal. |
| Lua (embedded) | Scripting language | Interpreter ~100–200 kB; no native bytecode compression; not bare-metal. |
| Forth | Stack-based language | Closest ancestor. Tenuis adds native LZ77+Huffman compression and an explicit, CI-enforced size budget. |
| WebAssembly | Portable bytecode | Specification targets browser/OS environments; runtime is 100s of kB to MB. |

## Versioning and Longevity

The binary format and instruction set carry an explicit version number in the `.tenb` header (see `BYTECODE_FORMAT.md`). The intent is that a spec-only reimplementation — written decades from now with no access to this codebase — can read and execute any version-1 Tenuis binary from the complete documentation in this `docs/` directory.

To support this:

1. All format fields are little-endian and precisely sized.
2. All opcodes have fixed, documented semantics with no implementation-defined behaviour.
3. The CRC32 in the header allows future readers to verify binary integrity independently.
4. Reserved opcode slots are explicitly documented and trigger `HALT` in the current version.
5. This document and all spec documents in `docs/` are part of the binary format's permanent record.
