#pragma once
#include <protocol/bootinfo.h>
#include <stdint.h>

typedef struct {
    bootinfo_kernel_entry_point_t entry_point;

    bootinfo_segment_t* segments;
    size_t segment_count;

    bootinfo_kernel_info_t* kernel_info;
} elf_loader_info_t;

bool elf_load_kernel(elf_loader_info_t* out_info);
