# Tenuis Instruction Set Reference

Version: 1.0  
Status: Normative

## Overview

The Tenuis virtual machine is a **stack-based interpreter** with the following characteristics:

- **Value stack**: holds 32-bit signed integers (`int32_t`). Up to `TENUIS_STACK_DEPTH` entries (default: 64).
- **Return stack**: holds 16-bit return addresses (separate from the value stack, not accessible from user code). Up to `TENUIS_RSTACK_DEPTH` entries (default: 32).
- **Data memory**: a flat byte-addressed array of `TENUIS_DATA_SIZE` bytes (default: 4 096). Accessed via `LOAD`/`STORE` instructions.
- **Code memory**: the decompressed bytecode, up to `TENUIS_CODE_SIZE` bytes (default: 8 192). Read-only during execution.
- **Program counter (PC)**: a 16-bit unsigned integer indexing into code memory.
- **Endianness**: all multi-byte literals in bytecode are **little-endian**.
- **Integer arithmetic**: wraps on overflow (two's complement, no traps).
- **Division by zero**: causes `HALT` with an implementation-defined diagnostic. (On bare-metal, this typically means no output is produced after the fault.)

### Stack Effect Notation

Operations are described using Forth-style stack diagrams:

```
( before -- after )
```

Items are listed left to right with the top of stack (TOS) on the right. `( a b -- c )` means: `a` is the second item, `b` is the TOS; after the operation, both are consumed and `c` is the new TOS. An empty slot is written `--` with nothing on that side.

## Source Syntax

Tenuis source files (extension `.ten`) are UTF-8 text. The grammar is token-based: tokens are separated by whitespace (space, tab, newline). Each token is one of:

### Integer Literals

A decimal integer with an optional leading `-`:

```
Regex: -?[0-9]+
```

The compiler emits the shortest encoding:
- `PUSH8`  if the value fits in `int8_t` (−128 to 127)
- `PUSH16` if the value fits in `int16_t` (−32 768 to 32 767)
- `PUSH32` otherwise

### Single-Character Operation Tokens

Each of the characters in the table below (when appearing as a standalone whitespace-delimited token) compiles directly to its associated opcode.

### Control Flow Tokens

| Source form | Meaning |
|---|---|
| `:name` | Define label `name` at the current PC (no bytes emitted) |
| `#name` | Unconditional jump: emits `JMP addr` where `addr` is the address of `name` |
| `?name` | Conditional jump: emits `JZ addr`; pops flag, jumps if flag == 0 |
| `(name)` | Subroutine call: emits `CALL addr` where `addr` is the address of `name` |
| `;` | Return from subroutine: emits `RET` |

Labels must be defined before they are referenced in a forward jump (`#` or `?`) unless the compiler performs two-pass backpatching (Phase 2 compiler does two-pass, so forward references are permitted). Label names may contain ASCII letters, digits, and underscores, and must start with a letter or underscore.

### Port Operation Tokens

Port operations address numbered I/O channels (0-255) through a configurable bus. The port number is encoded as a decimal integer immediately following the token letter, with no whitespace.

| Source form | Meaning |
|---|---|
| `W<n>` | Write low byte of TOS to port `n` (0-255). Emits `WRITE_PORT n`. Pops the value. |
| `R<n>` | Read one byte from port `n` (0-255). Emits `READ_PORT n`. Pushes the byte zero-extended. |

Port numbers must be in the range 0-255. The compiler rejects values outside this range at compile time. Port 0 is the default channel (stdin/stdout on the host; configurable on the target). Port assignments for other channels are defined by the integrator via the `PortBus` interface (see `docs/SPACE_PROFILE.md`).

The single-character `.` (EMIT) and backtick (READ) are shorthand for port 0 and compile to a different opcode (`0x50`/`0x51`) with no inline port byte. `W0` and `R0` compile to `0x52 0x00` and `0x53 0x00` respectively — functionally equivalent at runtime since both route through the same bus on port 0.

### Comments

`//` begins a line comment. Everything from `//` to the end of the line is ignored.

## Complete Opcode Table

Encoding types:
- **I** — 1 byte: opcode only
- **L** — 2 bytes: opcode + `int8_t` (sign-extended to `int32_t` on the stack)
- **W** — 3 bytes: opcode + `uint16_t` little-endian
- **D** — 5 bytes: opcode + `int32_t` little-endian

### Stack Manipulation

| Hex  | Mnemonic | Source | Enc | Bytes | Stack Effect | Description |
|------|----------|--------|-----|-------|--------------|-------------|
| 0x00 | NOP      | —      | I   | 1     | ( -- )       | No operation. |
| 0x01 | HALT     | `_`    | I   | 1     | ( -- )       | Stop execution immediately. The VM exits cleanly. |
| 0x02 | PUSH8    | *int*  | L   | 2     | ( -- n )     | Push the `int8_t` operand, sign-extended to `int32_t`. |
| 0x03 | PUSH16   | *int*  | W   | 3     | ( -- n )     | Push the `int16_t` operand (little-endian), sign-extended to `int32_t`. |
| 0x04 | PUSH32   | *int*  | D   | 5     | ( -- n )     | Push the `int32_t` operand (little-endian). |
| 0x05 | POP      | `,`    | I   | 1     | ( n -- )     | Discard the top of stack. |
| 0x06 | DUP      | `$`    | I   | 1     | ( n -- n n ) | Duplicate the top of stack. |
| 0x07 | SWAP     | `"`    | I   | 1     | ( a b -- b a ) | Swap the top two stack items. |
| 0x08 | OVER     | `'`    | I   | 1     | ( a b -- a b a ) | Copy the second item to the top. |
| 0x09 | ROT      | —      | I   | 1     | ( a b c -- b c a ) | Rotate: move the third item to the top. |

### Arithmetic

| Hex  | Mnemonic | Source | Enc | Bytes | Stack Effect | Description |
|------|----------|--------|-----|-------|--------------|-------------|
| 0x10 | ADD      | `+`    | I   | 1     | ( a b -- a+b ) | Signed 32-bit addition. Wraps on overflow. |
| 0x11 | SUB      | `-`    | I   | 1     | ( a b -- a-b ) | Signed subtraction: `a − b`. |
| 0x12 | MUL      | `*`    | I   | 1     | ( a b -- a*b ) | Signed multiplication. Result is low 32 bits. |
| 0x13 | DIV      | `/`    | I   | 1     | ( a b -- a/b ) | Signed integer division, truncated toward zero. Division by zero halts. |
| 0x14 | MOD      | `%`    | I   | 1     | ( a b -- a%b ) | Signed modulo. Sign of result matches dividend (`a`). Division by zero halts. |
| 0x15 | NEG      | —      | I   | 1     | ( a -- -a )  | Arithmetic negation. `INT32_MIN` negated remains `INT32_MIN` (wraps). |
| 0x16 | INC      | —      | I   | 1     | ( a -- a+1 ) | Increment by 1. Wraps on overflow. |
| 0x17 | DEC      | —      | I   | 1     | ( a -- a-1 ) | Decrement by 1. Wraps on underflow. |

> **Compiler note**: `NEG`, `INC`, and `DEC` have no source-character forms. The compiler may emit them as optimisations (e.g., `1 -` → `DEC`, `0 [literal] SUB` → `[literal] NEG`), but source writers cannot write them directly. This keeps the source alphabet compact without sacrificing the opcode's usefulness for code density.

### Bitwise

| Hex  | Mnemonic | Source | Enc | Bytes | Stack Effect | Description |
|------|----------|--------|-----|-------|--------------|-------------|
| 0x18 | AND      | `&`    | I   | 1     | ( a b -- a&b ) | Bitwise AND. |
| 0x19 | OR       | `\|`   | I   | 1     | ( a b -- a\|b ) | Bitwise OR. |
| 0x1A | XOR      | `^`    | I   | 1     | ( a b -- a^b ) | Bitwise XOR. |
| 0x1B | BITNOT   | `~`    | I   | 1     | ( a -- ~a )  | Bitwise complement (all bits flipped). |
| 0x1C | SHL      | `[`    | I   | 1     | ( a n -- a<<n ) | Logical shift left. `n` is treated as unsigned; shifts ≥ 32 yield 0. |
| 0x1D | SHR      | `]`    | I   | 1     | ( a n -- a>>n ) | Logical (unsigned) shift right. `a` treated as `uint32_t`. Shifts ≥ 32 yield 0. |
| 0x1E | SAR      | —      | I   | 1     | ( a n -- a>>n ) | Arithmetic shift right (sign-filling). `a` treated as `int32_t`. Shifts ≥ 32 yield 0 or −1. |

### Comparison

All comparison instructions pop two items, compare them, and push a boolean: **1 if true, 0 if false**. All comparisons are **signed** (treat stack values as `int32_t`). For unsigned comparisons, the caller must mask or convert beforehand.

| Hex  | Mnemonic | Source | Enc | Bytes | Stack Effect | Description |
|------|----------|--------|-----|-------|--------------|-------------|
| 0x20 | EQ       | `=`    | I   | 1     | ( a b -- flag ) | 1 if `a == b`. |
| 0x21 | NEQ      | —      | I   | 1     | ( a b -- flag ) | 1 if `a != b`. |
| 0x22 | LT       | `<`    | I   | 1     | ( a b -- flag ) | 1 if `a < b` (signed). |
| 0x23 | GT       | `>`    | I   | 1     | ( a b -- flag ) | 1 if `a > b` (signed). |
| 0x24 | LE       | —      | I   | 1     | ( a b -- flag ) | 1 if `a <= b` (signed). |
| 0x25 | GE       | —      | I   | 1     | ( a b -- flag ) | 1 if `a >= b` (signed). |

> `NEQ`, `LE`, `GE`, and `SAR` have no source character forms. They are emitted by the compiler as optimisations or dead-code eliminations. Source programs express `!=` as `= ~` (equal, then bitwise-not the boolean), `<=` as `> ~`, etc.

### Memory

Data memory is a flat byte array. Addresses are `uint16_t` (0–65 535). Out-of-range addresses halt the VM.

`LOAD` instructions pop an address from the value stack and push the value read from that address.  
`STORE` instructions pop an address (TOS), then pop a value (second), and write the value to that address.

| Hex  | Mnemonic | Source | Enc | Bytes | Stack Effect | Description |
|------|----------|--------|-----|-------|--------------|-------------|
| 0x30 | LOAD32   | `@`    | I   | 1     | ( addr -- val ) | Load 32-bit word (little-endian) from data memory at `addr`. |
| 0x31 | LOAD8    | —      | I   | 1     | ( addr -- val ) | Load 1 byte from data memory at `addr`, zero-extend to 32 bits. |
| 0x32 | LOAD16   | —      | I   | 1     | ( addr -- val ) | Load 16-bit half-word (little-endian) from data memory at `addr`, zero-extend. |
| 0x38 | STORE32  | `!`    | I   | 1     | ( val addr -- ) | Store 32-bit word (little-endian) to data memory at `addr`. |
| 0x39 | STORE8   | —      | I   | 1     | ( val addr -- ) | Store low 8 bits of `val` to data memory at `addr`. |
| 0x3A | STORE16  | —      | I   | 1     | ( val addr -- ) | Store low 16 bits of `val` (little-endian) to data memory at `addr`. |

> `LOAD8`, `LOAD16`, `STORE8`, `STORE16` have no source character forms. Use `@` and `!` for 32-bit access; the compiler emits the narrower forms when it can infer the data type from context (Phase 2+ feature).

### Control Flow

Jump targets are **absolute 16-bit addresses** within the code segment. Little-endian.

`CALL` pushes the return address (PC of the instruction immediately following `CALL`) onto the **return stack** and jumps to the target. `RET` pops the return stack and jumps to the popped address. The user-visible value stack is unaffected by `CALL`/`RET`.

| Hex  | Mnemonic | Source | Enc | Bytes | Stack Effect | Description |
|------|----------|--------|-----|-------|--------------|-------------|
| 0x40 | JMP      | `#lbl` | W   | 3     | ( -- )       | Unconditional jump to `uint16_t` target address. |
| 0x41 | JZ       | `?lbl` | W   | 3     | ( flag -- )  | Pop `flag`; if `flag == 0`, jump to target; else continue. |
| 0x42 | JNZ      | —      | W   | 3     | ( flag -- )  | Pop `flag`; if `flag != 0`, jump to target; else continue. |
| 0x43 | CALL     | `(lbl)`| W   | 3     | ( -- )       | Push return address onto return stack; jump to target. |
| 0x44 | RET      | `;`    | I   | 1     | ( -- )       | Pop return stack; jump to popped address. Return stack underflow halts. |

### I/O

All I/O is routed through a `PortBus` — a pair of function pointers configured per deployment target. Port 0 is the default channel (stdin/stdout on the host). The `EMIT` and `READ` instructions always address port 0. `WRITE_PORT` and `READ_PORT` address an arbitrary port whose number is encoded inline in the bytecode as a single byte immediately following the opcode.

If a bus function returns an error code, the VM halts immediately with a structured `HaltReason`:

| Bus return value | HaltReason |
|---|---|
| 0 | (success, continue) |
| -1 | `IO_UNAVAILABLE` — no handler registered for this port |
| -2 | `IO_TIMEOUT` — handler registered but peripheral did not respond |
| other negative | `IO_FAULT` — handler signalled a hardware fault |
| null function pointer | `IO_UNAVAILABLE` |

For `READ` and `READ_PORT`, end-of-file (the underlying `read()` syscall returning 0) is treated as `IO_TIMEOUT`.

| Hex  | Mnemonic   | Source  | Enc | Bytes | Stack Effect | Description |
|------|------------|---------|-----|-------|--------------|-------------|
| 0x50 | EMIT       | `.`     | I   | 1     | ( val -- )   | Write low byte of `val` to port 0 via the port bus. Halts on bus error. |
| 0x51 | READ       | `` ` `` | I   | 1     | ( -- val )   | Read one byte from port 0 via the port bus; push zero-extended to `int32_t`. EOF treated as `IO_TIMEOUT`. Halts on bus error. |
| 0x52 | WRITE_PORT | `W<n>`  | I+B | 2     | ( val -- )   | Write low byte of `val` to port `n` (inline byte, 0-255). Halts on bus error. |
| 0x53 | READ_PORT  | `R<n>`  | I+B | 2     | ( -- val )   | Read one byte from port `n` (inline byte, 0-255); push zero-extended. EOF treated as `IO_TIMEOUT`. Halts on bus error. |

`I+B` encoding: the opcode byte is followed immediately by one operand byte (the port number). This byte is consumed from the code stream, not the value stack.

Full `PortBus` interface specification and integration guide: `docs/SPACE_PROFILE.md`.

### Reserved / Undefined Opcodes

Any opcode byte not listed in this document is reserved. The current VM treats a reserved opcode as `HALT` with `HaltReason::ILLEGAL_OPCODE`. Assigned ranges:

| Range | Group |
|---|---|
| `0x00-0x09` | Stack manipulation |
| `0x10-0x17` | Arithmetic |
| `0x18-0x1E` | Bitwise |
| `0x20-0x25` | Comparison |
| `0x30-0x32`, `0x38-0x3A` | Memory |
| `0x40-0x44` | Control flow |
| `0x50-0x53` | I/O (Space Profile) |
| `0x60-0x6F` | Reserved — future widened arithmetic |
| `0x70-0x7F` | Reserved — future floating-point |
| `0x80-0x8F` | Reserved — future multi-core primitives |
| `0xF0-0xFE` | Platform extensions (implementation-defined) |
| `0xFF` | Permanently reserved; always `HALT` |

## Source Character Table (Quick Reference)

```
Char  Opcode    Operation
────  ────────  ──────────────────────────────
+     ADD       a b → a+b
-     SUB       a b → a-b
*     MUL       a b → a*b
/     DIV       a b → a/b (truncated)
%     MOD       a b → a%b
&     AND       a b → a&b
|     OR        a b → a|b
^     XOR       a b → a^b
~     BITNOT    a → ~a
[     SHL       a n → a<<n
]     SHR       a n → a>>n (unsigned)
<     LT        a b → (a<b ? 1 : 0)
>     GT        a b → (a>b ? 1 : 0)
=     EQ        a b → (a==b ? 1 : 0)
@     LOAD32    addr → mem32[addr]
!     STORE32   val addr → (stores val at addr)
$     DUP       n → n n
,     POP       n → (discarded)
"     SWAP      a b → b a
'     OVER      a b → a b a
.     EMIT      val → (output byte to port 0)
`     READ      → byte_from_port_0
;     RET       (return from subroutine)
_     HALT      (stop)

W<n>  WRITE_PORT  val → (write low byte to port n, n in 0-255)
R<n>  READ_PORT   → byte_from_port_n            (n in 0-255)
```

The `W<n>` and `R<n>` tokens are not single-character — they are a letter immediately followed by a decimal integer. They compile to a two-byte sequence: opcode + port byte.

## Example Programs

All examples use the source syntax. Items on the same line are shown with `//` comments.

### Example 1 — Emit a byte

```
65 .   // push 65 ('A'), emit it
_      // halt
```

Bytecode (3 bytes uncompressed):
```
0x02 0x41   // PUSH8 65
0x50        // EMIT
0x01        // HALT
```

### Example 2 — Arithmetic

```
10 3 + .   // push 10, push 3, add (→13), emit low byte
_
```

### Example 3 — Conditional

```
42 0 =     // push 42, push 0, EQ → pushes 0 (false, 42 ≠ 0)
?skip      // JZ: pop flag; flag==0 means "42==0 is false", so we DO jump
65 .       // this line is NOT reached
:skip
_          // halt
```

Wait — a common point of confusion: `?label` jumps when the flag **is zero**. Zero means "condition was false". So `?skip` after `42 0 =` (which pushes 0, meaning "not equal") **will** jump, skipping the `65 .` line. This is the "jump if false" pattern.

To "jump if true", invert first: `42 0 = ~ ?label` — but `~` is bitwise-not (turns 0 into −1, not 1). Instead, use `JNZ` via a compiler-emitted `42 0 = ?afterjump #target :afterjump` pattern, or simply structure code so `?` means "skip the else block":

```
// Pattern: "if (x == 0) do_thing"
x_val 0 =    // push 1 if x==0, else 0
?end_if      // jump past do_thing if flag==0 (i.e., x != 0)
// ... do_thing ...
:end_if
```

### Example 4 — Loop (count down from 5 to 1, emitting each)

```
5              // loop counter on stack
:loop
  $  .        // dup (keep counter), emit it
  1 -         // decrement
  $ 0 >       // dup counter, push 0, GT: is counter > 0?
  ?end        // jump to end if counter <= 0 (flag==0)
  #loop       // jump back
:end
,              // drop remaining counter
_              // halt
```

### Example 5 — Subroutine (double a value)

```
7 (double) .  // push 7, call double, emit result
_

:double       // subroutine: ( n -- 2*n )
  2 *         // multiply by 2
  ;           // return
```

Subroutines may be defined after the call site because the compiler performs two-pass backpatching.

### Example 6 — Memory store and load

```
99 0 !     // store 99 at data memory address 0
0 @        // load from address 0 (→ 99)
.          // emit 99
_
```

## Behavioural Guarantees

These apply to all conforming Tenuis VM implementations:

1. **Stack underflow**: any instruction that pops from an empty value stack must `HALT` the VM. The behaviour is not undefined; it is a clean halt.
2. **Stack overflow**: pushing onto a full value stack must `HALT`.
3. **Return stack underflow**: `RET` on an empty return stack must `HALT`.
4. **Return stack overflow**: `CALL` on a full return stack must `HALT`.
5. **Out-of-bounds PC**: if the PC moves outside `[0, CODE_SIZE)`, the VM must `HALT`.
6. **Out-of-bounds memory**: accessing data memory outside `[0, DATA_SIZE)` must `HALT`.
7. **Arithmetic overflow**: wraps silently (two's complement). No trap.
8. **Division by zero**: `HALT`.
9. **Illegal opcode**: any opcode byte not assigned in the current version halts with `HaltReason::ILLEGAL_OPCODE`.
10. **I/O bus error**: any port operation whose bus function returns a negative value halts with the corresponding `HaltReason` (`IO_UNAVAILABLE`, `IO_TIMEOUT`, or `IO_FAULT`). The program cannot catch or resume from an I/O fault.
11. **Instruction budget**: if a budget is configured (`instruction_limit > 0`) and the count of executed instructions reaches the limit, the VM halts with `HaltReason::BUDGET_EXCEEDED`.
12. **Halt is idempotent**: after any halt, the VM does nothing further. All static state (stacks, memory, PC, `instructions_executed`) is preserved for inspection.

## Future Extension (Version 2 Notes)

The following are explicitly deferred to a future version and documented here to prevent accidental overlap with reserved opcode slots:

- **0x60–0x6F**: Reserved for future 32-bit and 64-bit widened arithmetic (e.g., `MUL64` producing a 64-bit result on two stack slots).
- **0x70–0x7F**: Reserved for future floating-point operations (if a mission requires them and the cost is justified).
- **0x80–0x8F**: Reserved for future multi-threading primitives (if a future version targets multi-core bare-metal targets).
- **0xF0–0xFE**: Reserved for platform extension instructions (implementation-defined, documented in the platform's companion spec).
- **0xFF**: Permanently reserved; always `HALT`.
