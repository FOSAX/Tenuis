#include "io.h"
#include <unistd.h>

static int stdio_write(void* /*ctx*/, uint8_t port, uint8_t value) {
    if (port != 0) return -1;
    (void)write(1, &value, 1);
    return 0;
}

static int stdio_read(void* /*ctx*/, uint8_t port, uint8_t* value) {
    if (port != 0) return -1;
    uint8_t b = 0u;
    const ssize_t n = read(0, &b, 1);
    if (n == 1) { *value = b; return 0; }
    if (n == 0) return -2;   // EOF: peripheral silent — treat as IO_TIMEOUT
    return -3;               // read() error: IO_FAULT
}

PortBus tenuis_stdio_bus() {
    return { nullptr, stdio_write, stdio_read };
}
