// TCF v1 encoder: LZ77 (window=4096, lazy) + canonical Huffman (max 15 bits, MSB-first).
// Host-side tool — no size constraint.

#include "compress.h"
#include <algorithm>
#include <cstring>
#include <queue>
#include <vector>

// ── LZ77 ─────────────────────────────────────────────────────────────────
struct Item {
    bool     is_match;
    uint8_t  lit;
    uint16_t offset;  // actual offset 1..4096  (valid when is_match)
    uint16_t length;  // actual length 3..258   (valid when is_match)
};

static void find_match(const uint8_t* data, uint32_t len, uint32_t pos,
                       uint16_t& best_off, uint16_t& best_len) {
    const uint32_t WINDOW = 4096u, MIN = 3u, MAX = 258u;
    best_off = 0; best_len = 0;
    const uint32_t start = (pos >= WINDOW) ? pos - WINDOW : 0;
    for (uint32_t i = start; i < pos; ++i) {
        uint32_t mlen = 0;
        while (mlen < MAX && pos + mlen < len && data[i + mlen] == data[pos + mlen])
            ++mlen;
        if (mlen >= MIN && mlen > best_len) {
            best_len = (uint16_t)mlen;
            best_off = (uint16_t)(pos - i);
        }
    }
}

static std::vector<Item> lz77_encode(const uint8_t* data, uint32_t len) {
    std::vector<Item> items;
    items.reserve(len);
    uint32_t i = 0;
    while (i < len) {
        uint16_t off1, len1;
        find_match(data, len, i, off1, len1);
        if (len1 >= 3u) {
            // One-step lazy matching: check if i+1 gives a better match
            if (i + 1 < len) {
                uint16_t off2, len2;
                find_match(data, len, i + 1, off2, len2);
                if (len2 > len1) {
                    items.push_back({false, data[i], 0, 0});
                    ++i;
                    items.push_back({true, 0, off2, len2});
                    i += len2;
                    continue;
                }
            }
            items.push_back({true, 0, off1, len1});
            i += len1;
        } else {
            items.push_back({false, data[i], 0, 0});
            ++i;
        }
    }
    return items;
}

// ── Huffman ───────────────────────────────────────────────────────────────
struct HNode {
    uint64_t freq;
    int      sym;   // >= 0 for leaf, -1 for internal node
    int      left, right;
};

static std::vector<uint8_t> build_lengths(const uint64_t freq[258]) {
    std::vector<HNode> pool;
    pool.reserve(516);

    auto cmp = [&](int a, int b) {
        return pool[a].freq > pool[b].freq; // min-heap
    };
    std::priority_queue<int, std::vector<int>, decltype(cmp)> pq(cmp);

    for (int s = 0; s < 258; ++s) {
        if (!freq[s]) continue;
        int idx = (int)pool.size();
        pool.push_back({freq[s], s, -1, -1});
        pq.push(idx);
    }

    std::vector<uint8_t> lens(258, 0);

    if (pq.empty()) return lens;
    if (pq.size() == 1) {
        lens[pool[pq.top()].sym] = 1;
        return lens;
    }

    while (pq.size() > 1) {
        int a = pq.top(); pq.pop();
        int b = pq.top(); pq.pop();
        int idx = (int)pool.size();
        pool.push_back({pool[a].freq + pool[b].freq, -1, a, b});
        pq.push(idx);
    }

    // Iterative DFS to extract depths
    struct Frame { int node; int depth; };
    std::vector<Frame> stk;
    stk.push_back({pq.top(), 0});
    while (!stk.empty()) {
        auto [n, d] = stk.back(); stk.pop_back();
        if (pool[n].sym >= 0) {
            lens[pool[n].sym] = (uint8_t)d;
        } else {
            stk.push_back({pool[n].left,  d + 1});
            stk.push_back({pool[n].right, d + 1});
        }
    }

    // Cap lengths at 15 (TCF constraint) and repair Kraft inequality if needed.
    // Kraft value using fixed-point: each symbol of length L uses 2^(15-L) units of 2^15.
    uint32_t kraft = 0;
    for (int s = 0; s < 258; ++s) {
        if (!lens[s]) continue;
        if (lens[s] > 15) lens[s] = 15;
        kraft += (1u << (15 - lens[s]));
    }
    // Increase shortest-code lengths until sum <= 2^15 (over-commitment fix).
    while (kraft > (1u << 15)) {
        int best = -1; uint8_t blen = 15;
        for (int s = 0; s < 258; ++s)
            if (lens[s] && lens[s] < blen) { blen = lens[s]; best = s; }
        if (best < 0) break;
        kraft -= (1u << (15 - lens[best]));
        lens[best]++;
        kraft += (1u << (15 - lens[best]));
    }

    return lens;
}

// ── Bit writer (MSB-first) ────────────────────────────────────────────────
struct BitWriter {
    std::vector<uint8_t> out;
    uint8_t cur  = 0;
    int     bits = 0;

    void write_bit(int b) {
        cur = (uint8_t)((cur << 1) | (b & 1));
        if (++bits == 8) { out.push_back(cur); cur = 0; bits = 0; }
    }

    void write_bits(uint32_t v, int n) {
        for (int i = n - 1; i >= 0; --i)
            write_bit((int)((v >> i) & 1u));
    }

    void flush() {
        if (bits > 0) {
            out.push_back((uint8_t)(cur << (8 - bits)));
            cur = 0; bits = 0;
        }
    }
};

// ── Public entry point ────────────────────────────────────────────────────
std::vector<uint8_t> tcf_compress(const uint8_t* data, uint32_t len) {
    // LZ77
    auto items = lz77_encode(data, len);

    // Frequency count over 258-symbol alphabet
    uint64_t freq[258] = {};
    for (const auto& it : items)
        freq[it.is_match ? 257 : (int)it.lit]++;
    freq[256]++;  // END always present

    // Huffman code lengths → canonical codes
    auto lens = build_lengths(freq);

    struct Entry { uint8_t len; uint16_t sym; };
    std::vector<Entry> sorted;
    for (int s = 0; s < 258; ++s)
        if (lens[s]) sorted.push_back({lens[s], (uint16_t)s});
    std::stable_sort(sorted.begin(), sorted.end(), [](const Entry& a, const Entry& b){
        return a.len < b.len || (a.len == b.len && a.sym < b.sym);
    });

    uint16_t codes[258] = {};
    {
        uint16_t c = 0; uint8_t prev = 0;
        for (const auto& e : sorted) {
            c         <<= (e.len - prev);
            codes[e.sym] = c++;
            prev          = e.len;
        }
    }

    // Bit stream
    BitWriter bw;
    for (const auto& it : items) {
        if (!it.is_match) {
            bw.write_bits(codes[(int)it.lit], lens[(int)it.lit]);
        } else {
            bw.write_bits(codes[257], lens[257]);
            bw.write_bits((uint32_t)(it.offset - 1u), 12);  // raw 0-based offset
            bw.write_bits((uint32_t)(it.length - 3u), 8);   // raw 0-based length
        }
    }
    bw.write_bits(codes[256], lens[256]);  // END
    bw.flush();

    // Assemble 264-byte TCF header + bit stream
    std::vector<uint8_t> result;
    result.reserve(264u + bw.out.size());
    // Magic 'TCF1'
    result.push_back(0x54u); result.push_back(0x43u);
    result.push_back(0x46u); result.push_back(0x31u);
    // num_symbols = 258 (LE uint16)
    result.push_back(0x02u); result.push_back(0x01u);
    // code_lengths[0..257]
    for (int s = 0; s < 258; ++s) result.push_back(lens[s]);
    // bit stream
    for (uint8_t b : bw.out) result.push_back(b);

    return result;
}
