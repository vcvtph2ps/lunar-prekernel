#pragma once
#include <stdint.h>

typedef struct {
    uint64_t core_id;
    uintptr_t stack;
    uintptr_t cpu_local;
    void* arch_pointer; // used to point to architecture specific boot info
} core_start_info_t;
