#include <nanoprintf/nanoprintf.h>

///
#include <arch/16550uart.h>
#include <arch/io.h>
#include <log.h>

extern bool g_arch_16550uart_works;

static void debug_putc(int c) {
    arch_io_port_write_u8(0xe9, (uint8_t) c);
}

static void serial_putc(int c) {
    if(g_arch_16550uart_works) { arch_16550uart_send((char) c); }
}

static void putc(int c, void* ctx) {
    (void) ctx;
    debug_putc(c);
    serial_putc(c);
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
