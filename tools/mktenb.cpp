// mktenb — build an uncompressed Tenuis binary (.tenb) from raw hex bytecode.
// Usage: mktenb output.tenb [hex-byte ...] [--data hex-byte ...]
// Example: mktenb hello.tenb 02 41 50 01
//
// This is a host-side development tool; no size constraint applies.
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>

static uint16_t crc16(const uint8_t* d, size_t n) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= static_cast<uint16_t>(static_cast<uint16_t>(d[i]) << 8);
        for (int j = 0; j < 8; ++j)
            crc = (crc & 0x8000u)
                ? static_cast<uint16_t>((crc << 1) ^ 0x1021u)
                : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

static uint32_t crc32(const uint8_t* d, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int j = 0; j < 8; ++j)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return crc ^ 0xFFFFFFFFu;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: mktenb output.tenb [hex-byte ...] [--data hex-byte ...]\n");
        return 1;
    }

    const char* outpath = argv[1];
    std::vector<uint8_t> code, data;
    bool in_data = false;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--data") == 0) { in_data = true; continue; }
        const unsigned long v = strtoul(argv[i], nullptr, 16);
        if (in_data) data.push_back(static_cast<uint8_t>(v));
        else         code.push_back(static_cast<uint8_t>(v));
    }

    if (code.size() > 0xFFFFu) { fprintf(stderr, "mktenb: code too large\n"); return 1; }
    if (data.size() > 0xFFFFu) { fprintf(stderr, "mktenb: data too large\n"); return 1; }

    const uint16_t code_sz = static_cast<uint16_t>(code.size());
    const uint16_t data_sz = static_cast<uint16_t>(data.size());

    // Build header (20 bytes)
    uint8_t hdr[20] = {};
    hdr[0] = 0x54; hdr[1] = 0x45; hdr[2] = 0x4E; // magic TEN
    hdr[3] = 0x01; hdr[4] = 0x01;                  // version copy + version
    // flags: bit2 = F_LITTLE_END; bit1 = F_HAS_DATA if data present; bit0 = F_COMPRESSED = 0
    hdr[5] = static_cast<uint8_t>(0x04u | (data_sz ? 0x02u : 0x00u));
    hdr[6] = static_cast<uint8_t>(code_sz);        // code_uncompressed lo
    hdr[7] = static_cast<uint8_t>(code_sz >> 8);   // code_uncompressed hi
    hdr[8] = hdr[6]; hdr[9] = hdr[7];              // code_stored == code_uncompressed
    hdr[10] = static_cast<uint8_t>(data_sz);
    hdr[11] = static_cast<uint8_t>(data_sz >> 8);
    hdr[12] = 0; hdr[13] = 0;                      // entry_point = 0

    const uint16_t hcrc = crc16(hdr, 14);
    hdr[14] = static_cast<uint8_t>(hcrc);
    hdr[15] = static_cast<uint8_t>(hcrc >> 8);

    // Payload CRC32 covers code segment then data segment (in one pass)
    std::vector<uint8_t> payload;
    payload.insert(payload.end(), code.begin(), code.end());
    payload.insert(payload.end(), data.begin(), data.end());
    const uint32_t pcrc = crc32(payload.data(), payload.size());
    hdr[16] = static_cast<uint8_t>(pcrc);
    hdr[17] = static_cast<uint8_t>(pcrc >> 8);
    hdr[18] = static_cast<uint8_t>(pcrc >> 16);
    hdr[19] = static_cast<uint8_t>(pcrc >> 24);

    FILE* f = fopen(outpath, "wb");
    if (!f) { perror(outpath); return 1; }
    fwrite(hdr,          1, 20,          f);
    fwrite(code.data(),  1, code.size(), f);
    if (!data.empty())
        fwrite(data.data(), 1, data.size(), f);
    fclose(f);

    const size_t total = 20 + code.size() + data.size();
    printf("wrote %s  (%zu bytes: 20 hdr + %u code + %u data)\n",
           outpath, total, (unsigned)code.size(), (unsigned)data.size());
    return 0;
}
