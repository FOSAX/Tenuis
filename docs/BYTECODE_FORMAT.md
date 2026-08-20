# Tenuis Binary Format

Version: 1.0  
Status: Normative

## Overview

A Tenuis binary file (extension `.tenb`) is a self-contained, position-independent program unit. It carries:

1. A fixed 20-byte header with an integrity check.
2. A compressed code segment (bytecode, compressed with the Tenuis Compression Format described in `COMPRESSION_SPEC.md`).
3. An optional initialised data segment (copied verbatim into data memory on startup).

No file system, dynamic linker, operating system, or external runtime library is required to execute a `.tenb` binary, provided a compatible Tenuis runtime is present. The header version field enables runtimes to detect and reject incompatible future formats.

**All multi-byte integer fields are little-endian** (LSB at the lowest address). This is stated once here and applies to every field throughout this document.

## File Layout

```
Offset (bytes)   Size   Field
──────────────   ────   ─────────────────────────────────────────────────
0                4      Magic identifier
4                1      Format version
5                1      Flags
6                2      Code size (uncompressed), uint16
8                2      Code size (stored / compressed), uint16
10               2      Data segment size, uint16
12               2      Entry point offset into code segment, uint16
14               2      Header CRC16, uint16
16               4      Payload CRC32 (covers code + data segments as stored), uint32
20               cs     Code segment  (cs = value of "Code size stored" field)
20+cs            ds     Data segment  (ds = value of "Data segment size" field)
```

Total header size: **20 bytes**.  
Minimum valid file size: **20 bytes** (header only; empty program, zero code, zero data).  
Maximum file size: 20 + 65 535 + 65 535 = **131 090 bytes** (~128 KB).

## Header Fields

### Magic Identifier (bytes 0–3)

```
0x54  'T'
0x45  'E'
0x4E  'N'
0x01  (format version, same as byte 4)
```

The first three bytes identify the file as a Tenuis binary. Byte 3 is a redundant copy of the format version (byte 4). A loader must verify that both copies agree.

Rationale: four-byte magic values are common practice for binary formats. Including the version in the magic allows a quick "wrong version" diagnosis without reading byte 4 separately.

### Format Version (byte 4)

`uint8`. Current value: **1**.

A runtime that does not recognise this version must refuse to execute the binary and emit a human-readable error if an output channel is available. It must not attempt to guess at or ignore the version.

### Flags (byte 5)

`uint8`. Bit field:

| Bit | Name | Meaning when set |
|-----|------|-----------------|
| 0   | `F_COMPRESSED` | Code segment is compressed (TCF format). **Must always be 1 in version 1.** |
| 1   | `F_HAS_DATA`   | Data segment is present and non-empty (`ds > 0`). |
| 2   | `F_LITTLE_END` | Explicit endianness marker (always 1 in version 1; reserved for future). |
| 3–7 | (reserved)   | Must be 0. A loader may warn but must not reject on reserved-bit mismatch. |

> `F_COMPRESSED` is always 1 in version 1 because the toolchain always compresses. A raw (uncompressed) mode is reserved for future use (version 2 may set `F_COMPRESSED = 0` for debugging builds); version-1 loaders may reject binaries with `F_COMPRESSED = 0`.

### Code Size Uncompressed (bytes 6–7)

`uint16_le`. The number of bytes in the code segment **after decompression**. The runtime allocates this many bytes in the code buffer before decompression begins.

A runtime must verify that this value does not exceed its `TENUIS_CODE_SIZE` compile-time limit before decompressing. If it exceeds the limit, the runtime must `HALT` with a "code too large" diagnostic.

### Code Size Stored (bytes 8–9)

`uint16_le`. The number of bytes in the code segment **as stored in the file** (i.e., after compression). This is the number of bytes the runtime reads from the file starting at offset 20.

If `code_size_stored > code_size_uncompressed`, the binary is malformed; the runtime must halt.

### Data Segment Size (bytes 10–11)

`uint16_le`. The number of bytes in the data segment. Zero if `F_HAS_DATA` is not set (and the two must be consistent: if `F_HAS_DATA` is 0, this field must be 0, and vice versa).

The data segment is copied verbatim into the beginning of data memory on startup. Any data memory beyond offset `ds` is zero-initialised.

### Entry Point (bytes 12–13)

`uint16_le`. The offset (in bytes) into the **decompressed** code segment at which execution begins. Normally 0 (execution starts at the first byte of the code segment). A non-zero entry point is used when the binary places library subroutines before the main program.

The runtime must verify that the entry point is less than `code_size_uncompressed`. If not, the runtime must halt.

### Header CRC16 (bytes 14–15)

`uint16_le`. A CRC-16/CCITT-FALSE checksum of bytes 0–13 (the first 14 bytes of the header). Computed with polynomial 0x1021, initial value 0xFFFF, no reflection, no final XOR.

The runtime computes this value independently and halts if it does not match.

Rationale: a 16-bit checksum costs only 2 bytes and catches accidental corruption of the header, which is the most critical part of the binary. The payload has a separate CRC32 for stronger protection.

### Payload CRC32 (bytes 16–19)

`uint32_le`. A CRC-32/ISO-HDLC checksum of the combined payload: the code segment (as stored, compressed) followed immediately by the data segment, in that order. Polynomial: 0xEDB88320, initial value 0xFFFFFFFF, reflected input and output, final XOR 0xFFFFFFFF.

The runtime computes this value after reading the payload and halts if it does not match.

## Code Segment

The code segment begins at file offset 20 and is exactly `code_size_stored` bytes long. It contains the bytecode compressed in Tenuis Compression Format (TCF). See `COMPRESSION_SPEC.md` for the complete format.

The runtime must:
1. Allocate a buffer of `code_size_uncompressed` bytes.
2. Call the decompressor with the compressed segment as input.
3. Verify that the decompressor produced exactly `code_size_uncompressed` bytes. If not, halt.
4. Use the decompressed buffer as the code memory for the VM.

The code segment is **never modified at runtime**. The code buffer is read-only once decompression is complete.

### Bytecode Structure

The decompressed code segment is a flat sequence of instructions in the order they execute (or are reachable via jumps). Instructions are variable-width (1–5 bytes) per `INSTRUCTION_SET.md`. There is no code-segment header within the compressed payload; the bytes begin immediately with the first instruction.

## Data Segment

The data segment begins at file offset `20 + code_size_stored` and is exactly `data_segment_size` bytes long. It contains the initial values for the first `data_segment_size` bytes of data memory.

At startup, the runtime:
1. Copies the data segment verbatim into `data_memory[0 .. data_segment_size - 1]`.
2. Zero-initialises `data_memory[data_segment_size .. TENUIS_DATA_SIZE - 1]`.

The data segment is stored uncompressed. Rationale: data segments are typically small (constants, lookup tables) and the added complexity of compressing them would not recover meaningful space given the 20-byte header overhead of a second TCF stream.

## Complete Binary Example

A minimal program that emits byte 65 ('A') and halts:

**Source** (`hello.ten`):
```
65 . _
```

**Raw bytecode** (before compression):
```
Offset  Hex   Mnemonic
0       0x02  PUSH8
1       0x41  (operand: 65 = 0x41)
2       0x50  EMIT
3       0x01  HALT
```
Raw size: 4 bytes.

**Binary file** (hex dump of complete `.tenb`, assuming trivial compressed payload `P` of length `cs`):
```
Offset  Hex            Field
00      54 45 4E 01    Magic + version
04      07             Flags: F_COMPRESSED | F_LITTLE_END (bits 0 and 2)
05      00             Reserved
06      04 00          code_size_uncompressed = 4
08      cs_lo cs_hi    code_size_stored = (compressed size)
0A      00 00          data_segment_size = 0
0C      00 00          entry_point = 0
0E      hh hh          header_crc16
10      cc cc cc cc    payload_crc32
14      ...            compressed code segment (cs bytes)
```

Total file size: 20 + `cs` bytes, where `cs ≤ 4` (compression cannot expand these 4 bytes by much; worst case TCF overhead is the 258-byte Huffman table, but for a 4-byte payload the stored size will be larger than uncompressed — this is an edge case for trivially small programs, not a concern at realistic program sizes).

## Integrity Verification Sequence

The runtime must perform these checks in order before executing any bytecode:

1. Read 20 bytes. If fewer than 20 bytes are available, halt with "truncated header".
2. Check bytes 0–2 == `0x54 0x45 0x4E`. If not, halt with "not a Tenuis binary".
3. Check byte 3 == byte 4. If not, halt with "corrupt header".
4. Check byte 4 == 1 (supported version). If not, halt with "unsupported version N".
5. Compute CRC-16 of bytes 0–13. Compare with bytes 14–15. If not equal, halt with "header CRC mismatch".
6. Check `code_size_stored <= code_size_uncompressed`. If not, halt with "malformed sizes".
7. Check `code_size_uncompressed <= TENUIS_CODE_SIZE`. If not, halt with "code too large".
8. Check `data_segment_size <= TENUIS_DATA_SIZE`. If not, halt with "data too large".
9. Check `entry_point < code_size_uncompressed`. If not, halt with "invalid entry point".
10. Read `code_size_stored + data_segment_size` bytes (the payload). If fewer bytes available, halt with "truncated payload".
11. Compute CRC-32 over the payload. Compare with bytes 16–19. If not equal, halt with "payload CRC mismatch".
12. Decompress code segment. If decompression fails or output length ≠ `code_size_uncompressed`, halt with "decompression error".
13. Copy data segment into data memory. Zero-fill remainder.
14. Set PC to `entry_point`. Begin execution.

## Versioning and Long-Term Readability

The format is designed to remain self-describing for decades. Each binary carries:

- Its own version number (byte 4).
- Explicit sizes for every variable-length section.
- Two independent integrity checks (header CRC16 + payload CRC32).
- An explicit endianness marker in the flags field.

A reimplementation written in the year 2080 from this document alone can:
1. Identify a Tenuis binary by its magic bytes.
2. Determine the exact byte offsets of every section from the header.
3. Verify integrity before attempting any execution.
4. Determine the version and potentially refuse incompatible versions gracefully.

### Forward Compatibility

A version-1 runtime that encounters a version-2 binary must refuse to execute it. It must not guess at the layout, because future versions may change field sizes.

### Backward Compatibility

A version-2 (or later) runtime may choose to support version-1 binaries if the version-2 runtime documents this explicitly. There is no requirement to maintain backward compatibility; the version field exists precisely so that a clean break is possible.

### Reserved Fields

The `reserved` byte (offset 5) and reserved flag bits must be zero in all version-1 binaries. A version-1 runtime that encounters non-zero reserved fields should warn but must not refuse to execute (to allow future toolchains to begin setting reserved fields before a runtime update propagates).

## CRC Algorithms Reference

The following are the exact CRC parameters used. A reimplementor must use precisely these parameters.

### CRC-16/CCITT-FALSE (header check)

| Parameter | Value |
|-----------|-------|
| Width | 16 bits |
| Polynomial | 0x1021 |
| Initial value | 0xFFFF |
| Input reflection | No |
| Output reflection | No |
| Final XOR | 0x0000 |
| Check value (input "123456789") | 0x29B1 |

### CRC-32/ISO-HDLC (payload check)

| Parameter | Value |
|-----------|-------|
| Width | 32 bits |
| Polynomial | 0xEDB88320 (reflected form of 0x04C11DB7) |
| Initial value | 0xFFFFFFFF |
| Input reflection | Yes |
| Output reflection | Yes |
| Final XOR | 0xFFFFFFFF |
| Check value (input "123456789") | 0xCBF43926 |

These are the standard CRC-16/CCITT-FALSE and CRC-32 (Ethernet/PKzip/zlib) variants respectively. Both are widely documented and have known test vectors.
