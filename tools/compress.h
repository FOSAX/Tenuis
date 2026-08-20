#pragma once
#include <cstdint>
#include <vector>

// Compress raw bytecode using TCF v1 (LZ77 + canonical Huffman, MSB-first bits).
// Returns the compressed stream (264-byte header + bit stream), or empty on failure.
std::vector<uint8_t> tcf_compress(const uint8_t* data, uint32_t len);
