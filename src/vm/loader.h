#pragma once
#include <cstdint>

enum class LoadResult : uint8_t {
    OK = 0,
    TRUNCATED,
    BAD_MAGIC,
    BAD_VERSION,
    HDR_CRC_FAIL,
    PAYLOAD_CRC_FAIL,
    CODE_TOO_LARGE,
    DATA_TOO_LARGE,
    BAD_ENTRY,
    BAD_SIZES,
};

struct LoadedProgram {
    const uint8_t* code;
    uint32_t       code_stored;
    uint32_t       code_uncompressed;
    const uint8_t* data_seg;
    uint32_t       data_size;
    uint16_t       entry_point;
    uint8_t        flags;
};

LoadResult     tenuis_load(const uint8_t* file, uint32_t file_size, LoadedProgram& out);
const char*    load_result_str(LoadResult r);
