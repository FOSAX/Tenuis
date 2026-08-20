#include "decompress.h"

// ── Canonical Huffman table (BSS — zero-initialised, not in .text) ────────
// Sorted by (code_length, symbol) ascending after build_table().
static uint8_t  g_clen[258];
static uint16_t g_code[258];
static uint16_t g_sym[258];
static int      g_n;

static void build_table(const uint8_t* lengths) {
    g_n = 0;
    for (int s = 0; s < 258; ++s) {
        const uint8_t L = lengths[s];
        if (!L) continue;
        // Insertion-sort by (L, s) ascending
        int i = g_n++;
        while (i > 0 && (g_clen[i-1] > L || (g_clen[i-1] == L && g_sym[i-1] > (uint16_t)s))) {
            g_clen[i] = g_clen[i-1];
            g_sym[i]  = g_sym[i-1];
            --i;
        }
        g_clen[i] = L;
        g_sym[i]  = (uint16_t)s;
    }
    // Assign canonical codes
    uint16_t c    = 0;
    uint8_t  prev = 0;
    for (int i = 0; i < g_n; ++i) {
        c         <<= (g_clen[i] - prev);
        g_code[i]   = c++;
        prev        = g_clen[i];
    }
}

// ── Bit reader (static state → no stack overhead) ─────────────────────────
static const uint8_t* g_src;
static uint32_t       g_src_end;
static uint32_t       g_pos;
static uint8_t        g_acc;
static int            g_acc_bits;

// Returns 0 or 1; -1 on stream exhaustion.
static int read_bit(void) {
    if (g_acc_bits == 0) {
        if (g_pos >= g_src_end) return -1;
        g_acc      = g_src[g_pos++];
        g_acc_bits = 8;
    }
    return (int)((g_acc >> (uint8_t)(--g_acc_bits)) & 1u);
}

// Read n bits (MSB-first) into *out.  Returns 0 on success, -1 on error.
static int read_bits(int n, uint32_t* out) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) {
        const int b = read_bit();
        if (b < 0) return -1;
        v = (v << 1) | (uint32_t)b;
    }
    *out = v;
    return 0;
}

// Decode one Huffman symbol using linear scan (sorted table → early break).
// Returns symbol (0–257) or -1 on error.
static int sym_decode(void) {
    uint16_t acc = 0;
    int      len = 0;
    for (;;) {
        const int b = read_bit();
        if (b < 0) return -1;
        acc = (uint16_t)((acc << 1) | (unsigned)b);
        ++len;
        for (int i = 0; i < g_n; ++i) {
            if (g_clen[i] > (uint8_t)len) break; // table is len-sorted; no shorter codes remain
            if (g_clen[i] == (uint8_t)len && g_code[i] == acc)
                return (int)g_sym[i];
        }
        if (len > 15) return -1;
    }
}

// ── Public entry point ────────────────────────────────────────────────────
uint32_t tenuis_decompress(const uint8_t* src, uint32_t src_len,
                            uint8_t*       dst, uint32_t dst_cap) {
    // Verify TCF v1 magic and fixed header size
    if (src_len < 264u) return 0;
    if (src[0] != 0x54u || src[1] != 0x43u || src[2] != 0x46u || src[3] != 0x31u) return 0;
    // num_symbols (LE uint16) must be 258 for TCF version 1
    const uint16_t nsym = (uint16_t)(src[4] | ((uint16_t)src[5] << 8));
    if (nsym != 258u) return 0;

    build_table(src + 6);   // code_lengths[0..257] at offset 6

    g_src      = src;
    g_src_end  = src_len;
    g_pos      = 264u;      // bit stream starts after 264-byte header
    g_acc      = 0;
    g_acc_bits = 0;

    uint32_t out = 0;
    for (;;) {
        const int sym = sym_decode();
        if (sym < 0) return 0;

        if (sym < 256) {
            if (out >= dst_cap) return 0;
            dst[out++] = (uint8_t)sym;
        } else if (sym == 256) {
            return out;                 // END — decompression complete
        } else {                        // sym == 257: MATCH
            uint32_t raw_off, raw_len;
            if (read_bits(12, &raw_off) < 0) return 0;
            if (read_bits(8,  &raw_len) < 0) return 0;
            const uint32_t offset = raw_off + 1u;   // 1..4096
            const uint32_t length = raw_len + 3u;   // 3..258
            if (offset > out) return 0;              // reference before buffer start
            for (uint32_t i = 0; i < length; ++i) {
                if (out >= dst_cap) return 0;
                dst[out] = dst[out - offset];       // handles overlapping (run-length) correctly
                ++out;
            }
        }
    }
}
