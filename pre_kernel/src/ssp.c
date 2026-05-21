#include <log.h>
#include <panic.h>
#include <stdint.h>

uint64_t __stack_chk_guard = 0xdeadbeefcafebabe; // NOLINT

[[noreturn]] void __stack_chk_fail(void) { // NOLINT
    panic("stack smashing detected\n");
}
