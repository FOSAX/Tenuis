# Tenuis Compression Format (TCF) Specification

Version: 1.0  
Status: Normative

## Overview

Tenuis bytecode is stored in a compressed form called **TCF** (Tenuis Compression Format). TCF uses two sequential compression stages:

1. **LZ77** — eliminates repeated byte sequences by replacing them with back-references to earlier occurrences. Applied first.
2. **Huffman coding** — encodes the resulting symbol stream with variable-length codes proportional to symbol frequency. Applied second.

The combination exploits two properties of compiled Tenuis bytecode:

- **Repetition**: common opcode sequences (function prologues, loop bodies) repeat. LZ77 captures these efficiently.
- **Non-uniform distribution**: some opcodes are far more common than others (e.g., `ADD`, `DUP`, `PUSH8`). Huffman coding assigns shorter codes to frequent symbols.

The decompressor embedded in the Tenuis runtime reverses these stages in order: Huffman decode first, then LZ77 expand. The decompressor must fit within the **2 048-byte** component budget.

All bit-level operations use **MSB-first bit order** within each byte (the most significant bit of the first byte is the first bit read from the stream).

## Stage 1: LZ77

### Parameters

| Parameter | Value | Notes |
|---|---|---|
| Window size | 4 096 bytes | Matches a 12-bit back-reference offset field |
| Minimum match length | 3 bytes | Matches shorter than 3 are not worthwhile (overhead > savings) |
| Maximum match length | 258 bytes | Encoded as `(raw_length_byte) + 3`; raw values 0–255 yield lengths 3–258 |
| Lazy matching | Enabled (toolchain only) | The encoder checks whether position+1 gives a longer match before emitting the current match. Improves compression ratio at no decompressor cost. |

### LZ77 Encoder (host toolchain — no size constraint)

The encoder operates on the raw bytecode byte sequence. It maintains a 4 096-byte sliding window of previously seen bytes.

At each position, the encoder:
1. Searches the sliding window for the longest match to the bytes starting at the current position.
2. If the best match length ≥ 3, and lazy-match check confirms this is not beat by starting one byte later: emit a `MATCH` item.
3. Otherwise: emit a `LIT` item for the current byte and advance one position.

The output of the LZ77 stage is a sequence of **items**, each of type:

- **LIT** — a single literal byte value (0–255).
- **MATCH** — a back-reference: `(offset, length)` where `offset ∈ [1, 4096]` and `length ∈ [3, 258]`.
- **END** — end-of-stream marker (exactly one, at the end of the item sequence).

The sliding window wraps: position `p` in the window refers to `output[(total_bytes_emitted - offset) mod 4096]`.

### LZ77 Decoder

The decoder maintains an output buffer (the code memory buffer). As items arrive from the Huffman decoder:

- **LIT `b`**: write byte `b` at the current output position; advance.
- **MATCH `(offset, length)`**: for each of `length` iterations, copy the byte from `output[current_pos - offset]` to `output[current_pos]` and advance. Note: when `offset < length`, bytes just written may be copied (run-length expansion is valid).
- **END**: stop. The output buffer is complete.

The decoder must verify that each MATCH's `offset` does not exceed the number of bytes already written (i.e., no reference before the start of output). If it does, halt with "decompression error".

## Stage 2: Huffman Coding

### Symbol Alphabet

After LZ77, the item stream is encoded over a **258-symbol Huffman alphabet**:

| Symbol range | Meaning |
|---|---|
| 0–255 | Literal byte (LIT items) |
| 256 | END-of-stream |
| 257 | MATCH item (followed by raw offset and length fields in the bit stream) |

Only symbols actually present in the item stream are assigned non-zero code lengths.

### Huffman Table Construction (encoder)

The encoder performs a two-pass process:

**Pass 1 — frequency count**: scan the entire LZ77 item sequence and count occurrences of each symbol in the 258-symbol alphabet. Symbol 256 (END) always has frequency 1.

**Pass 2 — Huffman tree**: build a canonical Huffman tree from the frequency table using a min-heap (priority queue). Limit maximum code length to **15 bits** (re-merge as in length-limited Huffman if any code would exceed 15 bits).

**Canonical form**: sort symbols by (code_length ascending, symbol_value ascending). Assign codes by the canonical algorithm:

```
code ← 0
prev_len ← 0
for each (len, sym) in sorted order (len > 0):
    code ← code << (len - prev_len)
    assign code to sym
    code ← code + 1
    prev_len ← len
```

This produces a unique, reproducible code table from the code-length array alone. The decoder requires only the code-length array to reconstruct the same codes.

### Huffman Table Storage (in the TCF stream)

The Huffman table is stored at the start of the TCF byte stream, before the coded bit stream, in the following format:

```
Offset  Size    Field
0       4       TCF magic: 0x54 0x43 0x46 0x31  ('T','C','F','1')
4       2       num_symbols: uint16_le, number of entries in the code-length table
                Always 258 in TCF version 1.
6       258     code_lengths[0..257]: uint8 array.
                code_lengths[i] = 0 means symbol i is not used (no code assigned).
                code_lengths[i] ∈ [1, 15] is the Huffman code length for symbol i.
264     ...     Bit stream (see below)
```

The Huffman table section is **264 bytes** for all TCF version-1 streams. A decoder reads exactly 264 bytes to reconstruct the Huffman codes, then treats the remaining bytes as the bit stream.

Rationale: fixed-size table eliminates the need to parse a variable-length header, simplifying the decompressor. 258 bytes for the table plus 6 bytes of header = 264 bytes of constant overhead. For typical bytecode sizes (hundreds to thousands of bytes), this overhead is acceptable.

### Bit Stream

The bit stream immediately follows byte 264 of the TCF stream. It is a packed sequence of bits, MSB-first within each byte.

The decoder reads symbols as follows:

```
current_code ← 0
current_len ← 0
loop:
    read one bit from the bit stream → b
    current_code ← (current_code << 1) | b
    current_len ← current_len + 1
    if (current_code, current_len) matches a Huffman code:
        symbol ← matched symbol
        dispatch on symbol:
            0–255: emit LIT(symbol) to output
            256:   emit END; stop decoding
            257:   read MATCH fields (see below); emit MATCH
        reset current_code ← 0, current_len ← 0
    // continue looping (max 15 iterations per symbol)
```

A symbol is looked up using a standard Huffman decode table (array indexed by code value and length, or a canonical lookup table — implementation's choice, subject to the 2 048-byte decompressor budget).

### MATCH Fields

When symbol 257 is decoded, two raw (non-Huffman-coded) fields follow immediately in the bit stream:

| Field | Bits | Encoding |
|---|---|---|
| Offset | 12 | Unsigned integer in MSB-first order. Value range 0–4095. **Actual offset = value + 1** (range 1–4096). |
| Length | 8 | Unsigned integer in MSB-first order. Value range 0–255. **Actual length = value + 3** (range 3–258). |

A MATCH with offset=0 (raw) = offset 1 (actual) means "copy from one byte before the current write position". This is a run-length expansion when consecutive matches with offset=1 repeat.

After reading a MATCH's 20 raw bits, the Huffman decoder resumes from the next bit for the subsequent symbol.

## Bit Packing

The bit stream is packed MSB-first into bytes. Bits are consumed from the most significant bit of each byte:

```
Byte N:  bit 7 (first consumed) → ... → bit 0 (last consumed)
Byte N+1: bit 7 → ... → bit 0
...
```

The final byte of the bit stream may have unused padding bits (zeros) at the LSB end. The END symbol (256) terminates decoding before any padding bits are consumed, so padding does not affect correctness.

## Encoding Example

Input bytecode (8 bytes): `[0x02, 0x05, 0x10, 0x06, 0x02, 0x05, 0x10, 0x01]`

**LZ77 pass** (window=4096):
```
Position 0: byte 0x02 — no prior match, emit LIT(0x02)
Position 1: byte 0x05 — no prior match, emit LIT(0x05)
Position 2: byte 0x10 — no prior match, emit LIT(0x10)
Position 3: byte 0x06 — no prior match, emit LIT(0x06)
Position 4: bytes 0x02 0x05 0x10 — match at offset 4, length 3, emit MATCH(4, 3)
Position 7: byte 0x01 — no prior match, emit LIT(0x01)
emit END
```

LZ77 item stream: `LIT(0x02), LIT(0x05), LIT(0x10), LIT(0x06), MATCH(4,3), LIT(0x01), END`

Symbol frequencies for Huffman:
- Symbol 0x01: 1
- Symbol 0x02: 1
- Symbol 0x05: 1
- Symbol 0x06: 1
- Symbol 0x10: 1
- Symbol 256 (END): 1
- Symbol 257 (MATCH): 1

All 7 used symbols have equal frequency → all get the same Huffman code length (ceil(log2(7)) = 3 bits each).

With 7 symbols at 3 bits each, the bit stream length is (6 symbols × 3 bits) + (1 MATCH × 3 bits) + (20 raw MATCH bits) + (1 END × 3 bits) = 6×3 + 3 + 20 + 3 = **44 bits = 6 bytes** (rounded up from 44 bits).

Stored TCF size: 264 (header + table) + 6 (bit stream) = **270 bytes**. This is larger than the 8-byte input — compression is harmful at this scale, which is expected and acceptable. Compression benefits appear at program sizes of ~100 bytes and above.

## Decoder Implementation Notes

These are recommendations for the decompressor, not normative constraints on the format:

**Huffman lookup**: the most size-efficient implementation for a 15-bit max code length is a two-level lookup table: a direct-index table for codes up to N bits, and a linear scan for longer codes. N=8 (256 entries × (symbol + length) = ~512 bytes for the first level) is a reasonable trade-off within the 2 kB budget.

**No dynamic allocation**: all decoder state (the Huffman table, the sliding window, the output buffer) must fit in statically allocated arrays. The sliding window can be the output buffer itself (use output position modulo 4096 for the window pointer), eliminating the need for a separate 4 096-byte window buffer.

**In-place decompression**: the decoder writes directly to the code buffer (`uint8_t code[TENUIS_CODE_SIZE]`). The output buffer doubles as the LZ77 sliding window. This eliminates a separate 4 kB window allocation.

**Bit reader**: maintain a 32-bit accumulator and a bit count. Refill by reading one byte at a time from the compressed input. This is faster than bit-by-bit reads and compiles to small code on 32-bit targets.

**Termination**: stop as soon as END (symbol 256) is decoded. Do not read further bytes. The decompressor must return the number of bytes written to the output buffer; the loader verifies this equals `code_size_uncompressed`.

## Security Considerations

The decompressor must be robust against malformed input that could cause it to:

- Write beyond the bounds of the code buffer: check `output_position < TENUIS_CODE_SIZE` before every write.
- Read beyond the bounds of the compressed input: check remaining input length before reading each bit.
- Produce an infinite loop: the END symbol must always be present in the Huffman table (guaranteed by the encoder). If the bit stream is exhausted before END is decoded, halt with "decompression error".
- Reference output before the start of the buffer: check `offset ≤ output_position` before every MATCH copy.

## Compression Ratio Expectations

Based on typical Tenuis bytecode characteristics:

| Program type | Bytecode size | Expected compression ratio |
|---|---|---|
| Arithmetic expression evaluator | 50–200 B | 1.0–1.3× (marginal; header overhead dominates) |
| Small loop with subroutines | 200–1 000 B | 1.5–2.5× |
| Typical mission program | 1–8 KB | 2–4× |
| Large lookup-table program | 8–64 KB | 3–6× |

The 264-byte Huffman table overhead is fixed. For programs under ~200 bytes, TCF-compressed output may be larger than the raw bytecode. This is a known trade-off: the decompressor is always present in the runtime regardless, so there is no penalty to always compressing.

## TCF Version Identifier

The 4-byte magic `TCF1` at the start of every compressed segment allows the decompressor to verify it is receiving a TCF version-1 stream. If the magic does not match, the loader must halt with "invalid compression format". Future versions will use `TCF2`, `TCF3`, etc.
