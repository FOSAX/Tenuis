#pragma once
#include <cstdint>

// TCF v1 decompressor.  Returns bytes written, or 0 on any error.
// src/src_len: compressed TCF stream (including 264-byte header).
// dst/dst_cap: output buffer (must be >= code_size_uncompressed from .tenb header).
uint32_t tenuis_decompress(const uint8_t* src, uint32_t src_len,
                            uint8_t*       dst, uint32_t dst_cap);
