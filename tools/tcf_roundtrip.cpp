// tcf_roundtrip — compress then decompress a file; verify round-trip integrity.
// Usage: tcf_roundtrip <input-file>
// Prints compression stats to stderr; exits 0 on success, 1 on failure.

#include "compress.h"
#include "../src/vm/decompress.h"
#include <cstdio>
#include <cstring>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: tcf_roundtrip <input>\n");
        return 1;
    }

    // Read input file
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    const long fsz = ftell(f);
    rewind(f);
    if (fsz < 0) { fprintf(stderr, "tcf_roundtrip: seek error\n"); fclose(f); return 1; }
    std::vector<uint8_t> src(static_cast<size_t>(fsz));
    if (fsz > 0 && fread(src.data(), 1, static_cast<size_t>(fsz), f) != static_cast<size_t>(fsz)) {
        fprintf(stderr, "tcf_roundtrip: read error\n"); fclose(f); return 1;
    }
    fclose(f);

    // Compress
    const auto tcf = tcf_compress(src.data(), static_cast<uint32_t>(src.size()));
    if (tcf.empty() && !src.empty()) {
        fprintf(stderr, "tcf_roundtrip: compression failed\n");
        return 1;
    }

    // Decompress
    std::vector<uint8_t> dst(src.size());
    const uint32_t written = tenuis_decompress(
        tcf.data(), static_cast<uint32_t>(tcf.size()),
        dst.data(), static_cast<uint32_t>(dst.size()));

    if (written != static_cast<uint32_t>(src.size())) {
        fprintf(stderr, "tcf_roundtrip: FAIL — expected %zu bytes, got %u\n",
                src.size(), written);
        return 1;
    }
    if (memcmp(src.data(), dst.data(), src.size()) != 0) {
        fprintf(stderr, "tcf_roundtrip: FAIL — data mismatch after round-trip\n");
        return 1;
    }

    const double ratio = src.empty() ? 1.0
        : static_cast<double>(src.size()) / static_cast<double>(tcf.size());
    fprintf(stderr, "tcf_roundtrip: OK  %zu → %zu → %zu bytes  (%.2f× %s)\n",
            src.size(), tcf.size(), static_cast<size_t>(written),
            ratio,
            ratio > 1.0 ? "compression" : "expansion");
    return 0;
}
