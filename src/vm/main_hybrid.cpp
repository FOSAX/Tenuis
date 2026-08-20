#include "decompress.h"
#include "io.h"
#include "loader.h"
#include "vm.h"
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>

// Default program baked in at link time (compiled from the safe-mode .ten source).
extern const uint8_t  TENUIS_PROGRAM[];
extern const uint32_t TENUIS_PROGRAM_SIZE;

static VM      g_vm;
static uint8_t g_file_buf[20u + TENUIS_CODE_SIZE + TENUIS_DATA_SIZE];

static void emit_err(const char* msg) {
    (void)write(2, msg, strlen(msg));
    (void)write(2, "\n", 1);
}

// Returns true if the file at path was read and parsed successfully.
static bool try_load_file(const char* path, LoadedProgram& prog) {
    const int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    const ssize_t n = read(fd, g_file_buf, sizeof(g_file_buf));
    close(fd);
    if (n <= 0) return false;
    return tenuis_load(g_file_buf, static_cast<uint32_t>(n), prog) == LoadResult::OK;
}

// Initialises the VM from a LoadedProgram. Returns false on decompression failure.
static bool init_vm(const LoadedProgram& prog, const VMConfig& cfg) {
    if (prog.flags & 0x01u) {
        const uint32_t written = tenuis_decompress(
            prog.code, prog.code_stored,
            g_vm.code, prog.code_uncompressed);
        if (written != prog.code_uncompressed) return false;
        g_vm.code_size             = prog.code_uncompressed;
        memset(g_vm.data, 0, sizeof(g_vm.data));
        if (prog.data_seg && prog.data_size)
            memcpy(g_vm.data, prog.data_seg, prog.data_size);
        g_vm.pc                    = prog.entry_point;
        g_vm.sp                    = -1;
        g_vm.rsp                   = -1;
        g_vm.halt_reason           = HaltReason::OK;
        g_vm.instruction_limit     = cfg.instruction_limit;
        g_vm.instructions_executed = 0u;
        g_vm.ports                 = cfg.ports;
    } else {
        vm_init(g_vm,
                prog.code,    prog.code_stored,
                prog.data_seg, prog.data_size,
                prog.entry_point, cfg);
    }
    return true;
}

int main(int argc, char* argv[]) {
    uint32_t budget   = 0u;
    int      file_arg = 1;

    if (argc >= 3 && argv[1][0] == '-' && argv[1][1] == 'b' && argv[1][2] == '\0') {
        budget   = static_cast<uint32_t>(atol(argv[2]));
        file_arg = 3;
    }

    const VMConfig cfg = { budget, tenuis_stdio_bus() };
    LoadedProgram  prog;

    // ── Step 1: try uplinkable program (Mode A path) ─────────────────────────
    if (file_arg < argc) {
        if (try_load_file(argv[file_arg], prog) && init_vm(prog, cfg)) {
            const VMResult vr = vm_run(g_vm);
            if (vr.reason != HaltReason::OK) { emit_err(halt_reason_str(vr.reason)); return 2; }
            return 0;
        }
        emit_err("hybrid: uplink program rejected — falling back to embedded safe-mode");
    }

    // ── Step 2: fall back to embedded safe-mode program (Mode B path) ────────
    if (tenuis_load(TENUIS_PROGRAM, TENUIS_PROGRAM_SIZE, prog) != LoadResult::OK
        || !init_vm(prog, cfg)) {
        emit_err("hybrid: embedded safe-mode program corrupt");
        return 1;
    }

    const VMResult vr = vm_run(g_vm);
    if (vr.reason != HaltReason::OK) { emit_err(halt_reason_str(vr.reason)); return 2; }
    return 0;
}
