#include <common/mem.h>
#include <lib/math.h>
#include <log.h>
#include <memory/pagedb.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <panic.h>
#include <protocol/bootinfo.h>

extern bootinfo_t* g_boot_info;

void pagedb_setup(uintptr_t kernel_base, size_t pagedb_entry_size, uintptr_t* pfndb_start, size_t* pfndb_size) {
    size_t system_page_count = MATH_ALIGN_UP(g_boot_info->hhdm_size, PTM_PAGE_GRANULARITY) / PTM_PAGE_GRANULARITY;
    size_t pagedb_size = MATH_ALIGN_UP(system_page_count * pagedb_entry_size, PTM_PAGE_GRANULARITY);
    log_print("pagedb entry size: %zu\n", pagedb_entry_size);

    uintptr_t pagedb_base = g_boot_info->hhdm_offset + g_boot_info->hhdm_size + PTM_PAGE_GRANULARITY;
    uintptr_t pagedb_end = pagedb_base + pagedb_size;
    uintptr_t pagedb_mapped_end = pagedb_base;
    if(pagedb_end > kernel_base) { panic("pagedb overlaps with kernel image! pagedb_end=0x%016lx, kernel_base=0x%016lx", pagedb_end, kernel_base); }

    size_t frozen_map_size = g_pmm_map_size;
    pmm_map_entry_t* frozen_map = (pmm_map_entry_t*) ((uintptr_t) pmm_alloc(MATH_ALIGN_UP(sizeof(pmm_map_entry_t) * frozen_map_size, PTM_PAGE_GRANULARITY) / PTM_PAGE_GRANULARITY) + g_boot_info->hhdm_offset);
    memcpy(frozen_map, &g_pmm_map, sizeof(pmm_map_entry_t) * frozen_map_size);

    for(size_t i = 0; i < g_pmm_map_size; i++) {
        pmm_map_entry_t* entry = &g_pmm_map[i];
        switch(entry->type) {
            case PMM_MAP_TYPE_FREE:
            case PMM_MAP_TYPE_ALLOCATED:
            case PMM_MAP_TYPE_BOOTLOADER_RECLAIMABLE:
            case PMM_MAP_TYPE_EFI_RECLAIMABLE:
            case PMM_MAP_TYPE_ACPI_RECLAIMABLE:       break;
            default:                                  continue;
        }

        uintptr_t start = pagedb_base + MATH_FLOOR((entry->base / PTM_PAGE_GRANULARITY) * sizeof(pagedb_entry_size), PTM_PAGE_GRANULARITY);
        uintptr_t end = pagedb_base + MATH_CEIL(((entry->base + entry->length) / PTM_PAGE_GRANULARITY) * sizeof(pagedb_entry_size), PTM_PAGE_GRANULARITY);

        if(end <= pagedb_mapped_end) { continue; }

        log_print("Mapping page cache segment 0x%lx -> 0x%lx [0x%lx]\n", start, end, end - start);

        for(pagedb_end = start; pagedb_end < end; pagedb_end += PTM_PAGE_GRANULARITY) { ptm_map(pagedb_end, (uint64_t) pmm_alloc(1), PTM_PAGE_GRANULARITY, PTM_FLAG_READ | PTM_FLAG_WRITE); }
        pagedb_mapped_end = end;
    }

    *pfndb_start = pagedb_base;
    *pfndb_size = pagedb_size;
}
