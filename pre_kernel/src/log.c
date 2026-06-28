#include <arch.h>
#include <log.h>
#include <nanoprintf/nanoprintf.h>

static void putc(int c, void* ctx) {
    (void) ctx;
    arch_debug_putc((char) c);
}

void log_vprint_raw(const char* fmt, va_list val) {
    npf_vpprintf(putc, nullptr, fmt, val);
}

void log_print_raw(const char* fmt, ...) {
    va_list val;
    va_start(val, fmt);
    npf_vpprintf(putc, nullptr, fmt, val);
    va_end(val);
}


void log_vprint(const char* fmt, va_list val) {
    npf_vpprintf(putc, nullptr, "prekernel | ", nullptr);
    npf_vpprintf(putc, nullptr, fmt, val);
}

void log_print(const char* fmt, ...) {
    va_list val;
    va_start(val, fmt);
    npf_vpprintf(putc, nullptr, "prekernel | ", nullptr);
    npf_vpprintf(putc, nullptr, fmt, val);
    va_end(val);
}
