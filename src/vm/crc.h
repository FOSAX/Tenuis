#pragma once
#include <cstdint>
#include <cstddef>

uint16_t crc16_ccitt(const uint8_t* data, size_t len);
uint32_t crc32_iso(const uint8_t* data, size_t len);
