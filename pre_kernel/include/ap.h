#pragma once
#include <stdint.h>

typedef struct {
    uint64_t core_id;
    uintptr_t ap_stack;
    uintptr_t cpu_local;
    uintptr_t gdt_pointer;
} ap_boot_info_t;
