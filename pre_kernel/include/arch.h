#pragma once
#include <arch/gdt.h>
#include <boot/core.h>
#include <protocol/bootinfo.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    arch_gdt_t gdt;
    arch_gdt_tss_t tss;
} arch_core_start_info_t;

void arch_init_early();

// @note: index 0 is reserved for the BSP, APs start at index 1
void arch_setup_cpus(core_start_info_t* core_start_info_block, size_t core_count, bootinfo_kernel_info_t* kernel_image_info);
