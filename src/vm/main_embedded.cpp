#include "decompress.h"
#include "io.h"
#include "loader.h"
#include "vm.h"
#include <unistd.h>
#include <cstring>

// Program data injected at link time by tenuispack-generated object.
extern const uint8_t  TENUIS_PROGRAM[];
extern const uint32_t TENUIS_PROGRAM_SIZE;

// Static allocation keeps the VM off the OS stack.
static VM g_vm;

static void emit_err(const char* msg) {
    (void)write(2, msg, strlen(msg));
    (void)write(2, "\n", 1);
}

int main(void) {
    LoadedProgram prog;
    const LoadResult lr = tenuis_load(TENUIS_PROGRAM, TENUIS_PROGRAM_SIZE, prog);
    if (lr != LoadResult::OK) { emit_err(load_result_str(lr)); return 1; }

    const VMConfig cfg = { 0u, tenuis_stdio_bus() };

    if (prog.flags & 0x01u) {
        const uint32_t written = tenuis_decompress(
            prog.code,  prog.code_stored,
            g_vm.code,  prog.code_uncompressed);
        if (written != prog.code_uncompressed) {
            emit_err("decompression error"); return 1;
        }
        g_vm.code_size             = prog.code_uncompressed;
        memset(g_vm.data, 0, sizeof(g_vm.data));
        if (prog.data_seg && prog.data_size)
            memcpy(g_vm.data, prog.data_seg, prog.data_size);
        g_vm.pc                    = prog.entry_point;
        g_vm.sp                    = -1;
        g_vm.rsp                   = -1;
        g_vm.halt_reason           = HaltReason::OK;
        g_vm.instruction_limit     = 0u;
        g_vm.instructions_executed = 0u;
        g_vm.ports                 = cfg.ports;
    } else {
        vm_init(g_vm,
                prog.code,    prog.code_stored,
                prog.data_seg, prog.data_size,
                prog.entry_point, cfg);
    }

    const VMResult vr = vm_run(g_vm);
    if (vr.reason != HaltReason::OK) { emit_err(halt_reason_str(vr.reason)); return 2; }
    return 0;
}
