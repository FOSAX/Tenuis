#include "loader.h"
#include "crc.h"
#include "config.h"

// Binary format v1: see docs/BYTECODE_FORMAT.md
// Byte offsets:
//  0-2   magic TEN
//  3     version copy (= byte 4)
//  4     version (1)
//  5     flags
//  6-7   code_uncompressed (uint16 LE)
//  8-9   code_stored       (uint16 LE)
//  10-11 data_size         (uint16 LE)
//  12-13 entry_point       (uint16 LE)
//  14-15 header CRC16      (uint16 LE, covers bytes 0-13)
//  16-19 payload CRC32     (uint32 LE, covers bytes 20..)
//  20+   code segment (code_stored bytes)
//  20+cs data segment (data_size bytes)

LoadResult tenuis_load(const uint8_t* f, uint32_t sz, LoadedProgram& out) {
    if (sz < 20u)
        return LoadResult::TRUNCATED;

    if (f[0] != 0x54u || f[1] != 0x45u || f[2] != 0x4Eu)
        return LoadResult::BAD_MAGIC;

    if (f[3] != f[4])
        return LoadResult::BAD_VERSION;

    if (f[4] != 1u)
        return LoadResult::BAD_VERSION;

    // Header CRC covers bytes 0-13
    const uint16_t hdr_crc  = crc16_ccitt(f, 14u);
    const uint16_t file_hcrc = static_cast<uint16_t>(f[14] | (static_cast<uint16_t>(f[15]) << 8));
    if (hdr_crc != file_hcrc)
        return LoadResult::HDR_CRC_FAIL;

    const uint32_t code_unc  = static_cast<uint32_t>(f[6]  | (static_cast<uint16_t>(f[7])  << 8));
    const uint32_t code_stor = static_cast<uint32_t>(f[8]  | (static_cast<uint16_t>(f[9])  << 8));
    const uint32_t data_sz   = static_cast<uint32_t>(f[10] | (static_cast<uint16_t>(f[11]) << 8));
    const uint16_t entry     = static_cast<uint16_t>(f[12] | (static_cast<uint16_t>(f[13]) << 8));

    if (code_stor > code_unc)
        return LoadResult::BAD_SIZES;
    if (code_unc > TENUIS_CODE_SIZE)
        return LoadResult::CODE_TOO_LARGE;
    if (data_sz > TENUIS_DATA_SIZE)
        return LoadResult::DATA_TOO_LARGE;
    if (code_unc > 0u && static_cast<uint32_t>(entry) >= code_unc)
        return LoadResult::BAD_ENTRY;

    const uint32_t payload_sz = code_stor + data_sz;
    if (20u + payload_sz > sz)
        return LoadResult::TRUNCATED;

    const uint32_t file_pcrc = static_cast<uint32_t>(f[16])
                             | (static_cast<uint32_t>(f[17]) << 8)
                             | (static_cast<uint32_t>(f[18]) << 16)
                             | (static_cast<uint32_t>(f[19]) << 24);
    if (crc32_iso(f + 20, payload_sz) != file_pcrc)
        return LoadResult::PAYLOAD_CRC_FAIL;

    out.code              = f + 20;
    out.code_stored       = code_stor;
    out.code_uncompressed = code_unc;
    out.data_seg          = f + 20 + code_stor;
    out.data_size         = data_sz;
    out.entry_point       = entry;
    out.flags             = f[5];
    return LoadResult::OK;
}

const char* load_result_str(LoadResult r) {
    switch (r) {
        case LoadResult::OK:               return "ok";
        case LoadResult::TRUNCATED:        return "error: truncated file";
        case LoadResult::BAD_MAGIC:        return "error: not a Tenuis binary";
        case LoadResult::BAD_VERSION:      return "error: unsupported version";
        case LoadResult::HDR_CRC_FAIL:     return "error: header CRC mismatch";
        case LoadResult::PAYLOAD_CRC_FAIL: return "error: payload CRC mismatch";
        case LoadResult::CODE_TOO_LARGE:   return "error: code segment too large";
        case LoadResult::DATA_TOO_LARGE:   return "error: data segment too large";
        case LoadResult::BAD_ENTRY:        return "error: invalid entry point";
        case LoadResult::BAD_SIZES:        return "error: malformed size fields";
    }
    return "error: unknown";
}
