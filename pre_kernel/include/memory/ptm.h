#pragma once

#include <protocol/bootinfo.h>
#include <stddef.h>
#include <stdint.h>


#define PTM_FLAG_EXEC (1 << 0)
#define PTM_FLAG_WRITE (1 << 1)
#define PTM_FLAG_READ (1 << 2)

typedef enum : uint64_t {
    PTM_PAGE_SIZE_4K = 4096,
    PTM_PAGE_SIZE_2M = PTM_PAGE_SIZE_4K * 512,
    PTM_PAGE_SIZE_1G = PTM_PAGE_SIZE_2M * 512,
} ptm_page_size_t;

#define PTM_PAGE_GRANULARITY PTM_PAGE_SIZE_4K

typedef struct {
    uintptr_t tplt;
    size_t level_count;
} ptm_t;

extern ptm_t g_ptm;

void ptm_init();
void ptm_map(uint64_t vaddr, uint64_t paddr, uint64_t length, uint8_t flags);
void ptm_map_at(uintptr_t cr3, size_t level_count, uint64_t vaddr, uint64_t paddr, uint64_t length, uint8_t flags);
void ptm_create_hhdm_mappings();
