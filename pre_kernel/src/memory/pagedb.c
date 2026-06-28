#include <globals.h>
#include <lib/math.h>
#include <log.h>
#include <memory/pagedb.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <panic.h>
#include <protocol/bootinfo.h>
#include <runtime/mem.h>

void pagedb_setup(uintptr_t kernel_base, size_t pagedb_entry_size, uintptr_t* pfndb_start, size_t* pfndb_size) {
    size_t system_page_count = MATH_ALIGN_UP(g_globals_boot_info->hhdm_size, PTM_PAGE_GRANULARITY) / PTM_PAGE_GRANULARITY;
    size_t pagedb_size = MATH_ALIGN_UP(system_page_count * pagedb_entry_size, PTM_PAGE_GRANULARITY);
    log_print("pagedb entry size: %zu\n", pagedb_entry_size);

    uintptr_t pagedb_base = g_globals_boot_info->hhdm_offset + g_globals_boot_info->hhdm_size + PTM_PAGE_GRANULARITY;
    uintptr_t pagedb_end = pagedb_base + pagedb_size;
    uintptr_t pagedb_mapped_end = pagedb_base;
    if(pagedb_end > kernel_base) { panic("pagedb overlaps with kernel image! pagedb_end=0x%016lx, kernel_base=0x%016lx", pagedb_end, kernel_base); }

    pmm_map_snapshot_t snapshot = pmm_create_snapshot();

    for(size_t i = 0; i < snapshot.count; i++) {
        pmm_map_entry_t* entry = &snapshot.entries[i];
        switch(entry->type) {
            case PMM_MAP_TYPE_FREE:
            case PMM_MAP_TYPE_ALLOCATED:
            case PMM_MAP_TYPE_USED:
            case PMM_MAP_TYPE_BOOTLOADER_RECLAIMABLE:
            case PMM_MAP_TYPE_EFI_RECLAIMABLE:
            case PMM_MAP_TYPE_ACPI_RECLAIMABLE:
            case PMM_MAP_TYPE_ACPI_NVS:
            case PMM_MAP_TYPE_RESERVED:               break;
            default:                                  continue;
        }

        uintptr_t start = pagedb_base + MATH_FLOOR((entry->base / PTM_PAGE_GRANULARITY) * pagedb_entry_size, PTM_PAGE_GRANULARITY);
        uintptr_t end = pagedb_base + MATH_CEIL(((entry->base + entry->length) / PTM_PAGE_GRANULARITY) * pagedb_entry_size, PTM_PAGE_GRANULARITY);

        if(start < pagedb_mapped_end) { start = pagedb_mapped_end; }
        if(start >= end) { continue; }

        log_print("Mapping page cache segment 0x%lx -> 0x%lx [0x%lx]\n", start, end, end - start);

        for(pagedb_end = start; pagedb_end < end; pagedb_end += PTM_PAGE_GRANULARITY) { ptm_map(pagedb_end, (uint64_t) pmm_alloc_ext(1, PTM_PAGE_GRANULARITY, PMM_MAP_TYPE_USED), PTM_PAGE_GRANULARITY, PTM_FLAG_READ | PTM_FLAG_WRITE); }
        pagedb_mapped_end = end;
    }

    pmm_free_snapshot(&snapshot);
    *pfndb_start = pagedb_base;
    *pfndb_size = pagedb_size;
}
