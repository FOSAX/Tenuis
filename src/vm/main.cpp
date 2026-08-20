#include "decompress.h"
#include "io.h"
#include "loader.h"
#include "vm.h"
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>

// Static allocation: VM contains 12 kB of arrays — too large for the OS stack.
static VM g_vm;

// File buffer sized for max legal .tenb on Phase 1 (uncompressed, raw binary).
static uint8_t g_file_buf[20u + TENUIS_CODE_SIZE + TENUIS_DATA_SIZE];

static void emit_err(const char* msg) {
    (void)write(2, msg, strlen(msg));
    (void)write(2, "\n", 1);
}

int main(int argc, char* argv[]) {
    uint32_t budget   = 0u;
    int      file_arg = 1;

    if (argc >= 3 && argv[1][0] == '-' && argv[1][1] == 'b' && argv[1][2] == '\0') {
        budget   = static_cast<uint32_t>(atol(argv[2]));
        file_arg = 3;
    }

    if (file_arg >= argc) {
        emit_err("usage: tenuisr [-b <budget>] <program.tenb>");
        return 1;
    }

    const int fd = open(argv[file_arg], O_RDONLY);
    if (fd < 0) { emit_err("tenuisr: cannot open file"); return 1; }
    const ssize_t n = read(fd, g_file_buf, sizeof(g_file_buf));
    close(fd);
    if (n < 0) { emit_err("tenuisr: read error"); return 1; }

    LoadedProgram prog;
    const LoadResult lr = tenuis_load(g_file_buf, static_cast<uint32_t>(n), prog);
    if (lr != LoadResult::OK) { emit_err(load_result_str(lr)); return 1; }

    const VMConfig cfg = { budget, tenuis_stdio_bus() };

    if (prog.flags & 0x01u) {
        // Compressed: decompress TCF stream directly into code buffer (no second copy).
        const uint32_t written = tenuis_decompress(
            prog.code,            prog.code_stored,
            g_vm.code,            prog.code_uncompressed);
        if (written != prog.code_uncompressed) {
            emit_err("tenuisr: decompression error");
            return 1;
        }
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
        // Uncompressed: copy code directly into VM.
        vm_init(g_vm,
                prog.code,    prog.code_stored,
                prog.data_seg, prog.data_size,
                prog.entry_point, cfg);
    }

    const VMResult vr = vm_run(g_vm);

    if (vr.reason != HaltReason::OK) { emit_err(halt_reason_str(vr.reason)); return 2; }
    return 0;
}
