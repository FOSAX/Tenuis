// tenuisc — Tenuis compiler: source (.ten) → uncompressed Tenuis binary (.tenb)
// Phase 2: lexer + single-pass codegen with forward-reference backpatching.
//
// Source grammar (see docs/INSTRUCTION_SET.md):
//   integer      →  [-]?[0-9]+        compiles to PUSH8/PUSH16/PUSH32
//   single char  →  + - * / % & | ^ ~ [ ] < > = @ ! $ , " ' . ` ; _
//   :name        →  label definition (no bytes emitted)
//   #name        →  JMP  (unconditional jump)
//   ?name        →  JZ   (jump if top-of-stack == 0)
//   (name)       →  CALL
//   // ...       →  line comment
//
// Host-side tool — no size budget.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "../src/vm/crc.h"
#include "compress.h"

// ── Opcode table (source char → bytecode) ────────────────────────────────
static const struct { char c; uint8_t op; } OPTAB[] = {
    {'+', 0x10}, {'-', 0x11}, {'*', 0x12}, {'/', 0x13}, {'%', 0x14},
    {'&', 0x18}, {'|', 0x19}, {'^', 0x1A}, {'~', 0x1B},
    {'[', 0x1C}, {']', 0x1D},
    {'<', 0x22}, {'>', 0x23}, {'=', 0x20},
    {'@', 0x30}, {'!', 0x38},
    {'$', 0x06}, {',', 0x05}, {'"', 0x07}, {'\'', 0x08},
    {'.', 0x50}, {'`', 0x51},
    {';', 0x44}, {'_', 0x01},
};

static uint8_t opcode_for(char c) {
    for (const auto& e : OPTAB)
        if (e.c == c) return e.op;
    return 0;
}

// ── Compiler state ────────────────────────────────────────────────────────
struct Patch { uint16_t at; std::string name; };

static std::vector<uint8_t>                      g_code;
static std::unordered_map<std::string, uint16_t> g_labels;
static std::vector<Patch>                        g_patches;

static const char* g_pos   = nullptr;
static int         g_line  = 1;
static const char* g_file  = "<input>";

[[noreturn]] static void compile_error(const char* msg) {
    fprintf(stderr, "%s:%d: error: %s\n", g_file, g_line, msg);
    exit(1);
}

// ── Emit helpers ──────────────────────────────────────────────────────────
static void emit8(uint8_t b) { g_code.push_back(b); }

static void emit16le(uint16_t v) {
    emit8(static_cast<uint8_t>(v));
    emit8(static_cast<uint8_t>(v >> 8));
}

static void emit_push(int32_t v) {
    if (v >= -128 && v <= 127) {
        emit8(0x02);
        emit8(static_cast<uint8_t>(static_cast<int8_t>(v)));
    } else if (v >= -32768 && v <= 32767) {
        emit8(0x03);
        emit16le(static_cast<uint16_t>(static_cast<int16_t>(v)));
    } else {
        emit8(0x04);
        emit8(static_cast<uint8_t>(v));
        emit8(static_cast<uint8_t>(v >> 8));
        emit8(static_cast<uint8_t>(v >> 16));
        emit8(static_cast<uint8_t>(v >> 24));
    }
}

static void emit_branch(uint8_t opcode, const std::string& label) {
    emit8(opcode);
    auto it = g_labels.find(label);
    if (it != g_labels.end()) {
        emit16le(it->second);                     // backward reference: address known
    } else {
        g_patches.push_back({static_cast<uint16_t>(g_code.size()), label});
        emit16le(0);                               // forward reference: placeholder
    }
}

// ── Lexer utilities ───────────────────────────────────────────────────────
static void skip_ws_comments() {
    for (;;) {
        while (*g_pos == ' ' || *g_pos == '\t' || *g_pos == '\r' || *g_pos == '\n') {
            if (*g_pos == '\n') ++g_line;
            ++g_pos;
        }
        if (g_pos[0] == '/' && g_pos[1] == '/') {
            while (*g_pos && *g_pos != '\n') ++g_pos;
        } else {
            break;
        }
    }
}

static std::string parse_name() {
    std::string name;
    while (*g_pos && (isalnum((unsigned char)*g_pos) || *g_pos == '_'))
        name += *g_pos++;
    if (name.empty()) compile_error("expected identifier");
    return name;
}

static int32_t parse_integer() {
    const bool neg = (*g_pos == '-');
    if (neg) ++g_pos;
    if (!isdigit((unsigned char)*g_pos)) compile_error("expected digit");
    int32_t v = 0;
    while (isdigit((unsigned char)*g_pos))
        v = v * 10 + (*g_pos++ - '0');
    return neg ? -v : v;
}

// ── Main compilation pass ─────────────────────────────────────────────────
static void compile(const char* source, const char* filename) {
    g_pos  = source;
    g_file = filename;
    g_line = 1;

    while (*g_pos) {
        skip_ws_comments();
        if (!*g_pos) break;

        const char c = *g_pos;

        // Integer literal: digit, or '-' immediately followed by a digit
        if (isdigit((unsigned char)c) || (c == '-' && isdigit((unsigned char)g_pos[1]))) {
            emit_push(parse_integer());
            continue;
        }

        // Label definition:  :name
        if (c == ':') {
            ++g_pos;
            const std::string name = parse_name();
            const uint16_t    pc   = static_cast<uint16_t>(g_code.size());
            if (!g_labels.emplace(name, pc).second) {
                char buf[128];
                snprintf(buf, sizeof(buf), "duplicate label '%s'", name.c_str());
                compile_error(buf);
            }
            continue;
        }

        // Unconditional jump:  #name
        if (c == '#') {
            ++g_pos;
            emit_branch(0x40, parse_name());
            continue;
        }

        // Conditional jump (JZ, jump-if-zero):  ?name
        if (c == '?') {
            ++g_pos;
            emit_branch(0x41, parse_name());
            continue;
        }

        // Subroutine call:  (name)
        if (c == '(') {
            ++g_pos;
            const std::string name = parse_name();
            if (*g_pos != ')') compile_error("expected ')' after call name");
            ++g_pos;
            emit_branch(0x43, name);
            continue;
        }

        // Port write:  W<digits>  →  0x52 + port byte
        if (c == 'W' && isdigit((unsigned char)g_pos[1])) {
            ++g_pos;
            const int32_t port = parse_integer();
            if (port < 0 || port > 255) compile_error("port number out of range [0, 255]");
            emit8(0x52u);
            emit8(static_cast<uint8_t>(port));
            continue;
        }

        // Port read:   R<digits>  →  0x53 + port byte
        if (c == 'R' && isdigit((unsigned char)g_pos[1])) {
            ++g_pos;
            const int32_t port = parse_integer();
            if (port < 0 || port > 255) compile_error("port number out of range [0, 255]");
            emit8(0x53u);
            emit8(static_cast<uint8_t>(port));
            continue;
        }

        // Single-character opcode
        const uint8_t op = opcode_for(c);
        if (op) {
            ++g_pos;
            emit8(op);
            continue;
        }

        // Unrecognised
        char buf[64];
        snprintf(buf, sizeof(buf), "unexpected character '%c' (0x%02x)",
                 c, static_cast<unsigned char>(c));
        compile_error(buf);
    }

    // Backpatch forward references
    for (const auto& p : g_patches) {
        auto it = g_labels.find(p.name);
        if (it == g_labels.end()) {
            char buf[128];
            snprintf(buf, sizeof(buf), "undefined label '%s'", p.name.c_str());
            compile_error(buf);
        }
        const uint16_t addr = it->second;
        g_code[p.at]     = static_cast<uint8_t>(addr);
        g_code[p.at + 1] = static_cast<uint8_t>(addr >> 8);
    }
}

// ── Write .tenb output ────────────────────────────────────────────────────
static void write_tenb(const char* path) {
    if (g_code.size() > 0xFFFFu) {
        fprintf(stderr, "tenuisc: error: program too large (> 65535 bytes)\n");
        exit(1);
    }
    const uint16_t code_unc = static_cast<uint16_t>(g_code.size());

    // Try compression; only use it when the TCF stream is strictly smaller.
    std::vector<uint8_t> tcf;
    if (!g_code.empty()) {
        tcf = tcf_compress(g_code.data(), static_cast<uint32_t>(g_code.size()));
    }
    const bool use_compressed = !tcf.empty() && tcf.size() < g_code.size();

    const uint8_t*  payload  = use_compressed ? tcf.data()
                             : (g_code.empty() ? nullptr : g_code.data());
    const uint16_t  code_sto = use_compressed ? static_cast<uint16_t>(tcf.size())
                             : code_unc;
    // F_COMPRESSED (bit 0) | F_LITTLE_END (bit 2) = 0x05; uncompressed = 0x04
    const uint8_t   flags    = use_compressed ? 0x05u : 0x04u;

    uint8_t hdr[20] = {};
    hdr[0] = 0x54u; hdr[1] = 0x45u; hdr[2] = 0x4Eu;  // 'T','E','N'
    hdr[3] = 0x01u; hdr[4] = 0x01u;                    // version copy + version
    hdr[5] = flags;
    hdr[6] = static_cast<uint8_t>(code_unc);
    hdr[7] = static_cast<uint8_t>(code_unc >> 8);
    hdr[8] = static_cast<uint8_t>(code_sto);
    hdr[9] = static_cast<uint8_t>(code_sto >> 8);
    // hdr[10..13]: data_size=0, entry=0 (already zero-initialised)

    const uint16_t hcrc = crc16_ccitt(hdr, 14);
    hdr[14] = static_cast<uint8_t>(hcrc);
    hdr[15] = static_cast<uint8_t>(hcrc >> 8);

    const uint32_t pcrc = payload
        ? crc32_iso(payload, code_sto)
        : crc32_iso(nullptr, 0);
    hdr[16] = static_cast<uint8_t>(pcrc);
    hdr[17] = static_cast<uint8_t>(pcrc >> 8);
    hdr[18] = static_cast<uint8_t>(pcrc >> 16);
    hdr[19] = static_cast<uint8_t>(pcrc >> 24);

    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fwrite(hdr, 1, 20, f);
    if (payload && code_sto > 0)
        fwrite(payload, 1, code_sto, f);
    fclose(f);

    if (use_compressed)
        fprintf(stderr, "tenuisc: %s → %s  (%u → %u bytes, %.0f%% of original)\n",
                g_file, path,
                static_cast<unsigned>(g_code.size()),
                static_cast<unsigned>(tcf.size()),
                100.0 * static_cast<double>(tcf.size()) / static_cast<double>(g_code.size()));
    else
        fprintf(stderr, "tenuisc: %s → %s  (%u bytes, uncompressed)\n",
                g_file, path, static_cast<unsigned>(g_code.size()));
}

// ── Entry point ───────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: tenuisc <input.ten> <output.tenb>\n");
        return 1;
    }

    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    const long fsz = ftell(f);
    rewind(f);
    std::vector<char> buf(static_cast<size_t>(fsz) + 1);
    if (fread(buf.data(), 1, static_cast<size_t>(fsz), f) != static_cast<size_t>(fsz)) {
        fprintf(stderr, "tenuisc: read error\n"); return 1;
    }
    fclose(f);
    buf[fsz] = '\0';

    compile(buf.data(), argv[1]);
    write_tenb(argv[2]);
    return 0;
}
