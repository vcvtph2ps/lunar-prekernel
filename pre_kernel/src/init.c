#include <arch.h>
#include <arch/16550uart.h>
#include <arch/cr.h>
#include <arch/gdt.h>
#include <arch/machine.h>
#include <arch/msr.h>
#include <boot/boot.h>
#include <boot/core.h>
#include <lib/helpers.h>
#include <lib/math.h>
#include <loader/elfldr.h>
#include <log.h>
#include <memory/pagedb.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <panic.h>
#include <protocol/bootinfo.h>
#include <runtime/mem.h>


typedef void (*x86_64_kernel_entry_t)(bootinfo_kernel_entry_point_t entry, uintptr_t stack, uintptr_t page_tables, bootinfo_t* boot_info, uint64_t core_id);

extern uint8_t x86_64_kernel_handoff[];
extern uint8_t x86_64_kernel_handoff_end[];

static x86_64_kernel_entry_t g_boot_trampoline = nullptr;
static bootinfo_kernel_entry_point_t g_kernel_entry_point;

ATOMIC static uint32_t g_ap_init_lock = 0;
ATOMIC static uint32_t g_running_cores = 0;
static uint32_t g_core_count = 0;

__attribute__((no_sanitize("undefined"))) static void handoff_to_kernel(x86_64_kernel_entry_t trampoline, bootinfo_kernel_entry_point_t entry, uintptr_t stack, uintptr_t page_tables, bootinfo_t* boot_info, uint64_t core_id) {
    ATOMIC_LOAD_ADD(&g_running_cores, 1, ATOMIC_RELEASE);

    while(ATOMIC_LOAD(&g_running_cores, ATOMIC_ACQUIRE) < g_core_count) { asm volatile("pause"); }

    trampoline(entry, stack, page_tables, boot_info, core_id);
}


bootinfo_t* g_globals_boot_info = nullptr;

[[noreturn]] void prekernel_init_ap(core_start_info_t* boot_info) {
    while(ATOMIC_LOAD(&g_ap_init_lock, ATOMIC_ACQUIRE) == 0);

    arch_machine_init(boot_info);

    handoff_to_kernel(g_boot_trampoline, g_kernel_entry_point, boot_info->stack, g_ptm.tplt, nullptr, boot_info->core_id);
    while(1);
}

extern uint8_t _binary_kernel_elf_start[]; // NOLINT
extern uint8_t _binary_kernel_elf_end[]; // NOLINT

[[noreturn]] void prekernel_init(bootinfo_t* boot_info) {
    g_globals_boot_info = boot_info;
    arch_16550uart_early_setup();

    log_print("Hai :333\n");

    for(size_t i = 0; i < g_pmm_map_size; i++) {
        pmm_map_entry_t* entry = &g_pmm_map[i];
        log_print("pmm_entry[%zu]: base=0x%016lx, length=0x%016lx, type=%u\n", i, entry->base, entry->length, entry->type);
    }

    ptm_init();

    elfldr_loader_info_t kernel_image_info;
    if(!elfldr_load_kernel(&kernel_image_info)) { panic("failed to load kernel elf image"); }

    log_print("Kernel cpu local size: %zu\n", kernel_image_info.kernel_info->cpu_local_size);
    log_print("Kernel pagedb entry size: %zu\n", kernel_image_info.kernel_info->pagedb_entry_size);

    size_t hhdm_size = boot_info->hhdm_size;
    for(size_t i = 0; i < g_pmm_map_size; i++) hhdm_size = MATH_MAX(hhdm_size, g_pmm_map[i].base + g_pmm_map[i].length);
    boot_info->hhdm_size = hhdm_size;

    uintptr_t pfndb_start;
    size_t pfndb_size;
    pagedb_setup(kernel_image_info.kernel_base, kernel_image_info.kernel_info->pagedb_entry_size, &pfndb_start, &pfndb_size);
    boot_info->pfndb_start = pfndb_start;
    boot_info->pfndb_size = pfndb_size;

    // arch shit
    size_t start_info_block_size = MATH_ALIGN_UP(sizeof(core_start_info_t) * boot_info->core_count, PTM_PAGE_GRANULARITY);
    core_start_info_t* start_info_block = (core_start_info_t*) ((uintptr_t) pmm_alloc(start_info_block_size / PTM_PAGE_GRANULARITY) + g_globals_boot_info->hhdm_offset);
    memset(start_info_block, 0, start_info_block_size);

    arch_setup_cpus(start_info_block, boot_info->core_count, kernel_image_info.kernel_info);

    size_t size = MATH_ALIGN_UP((x86_64_kernel_handoff_end - x86_64_kernel_handoff), PTM_PAGE_GRANULARITY) / PTM_PAGE_GRANULARITY;
    log_print("boot trampoline size: %zu\n", size);
    void* boot_trampoline_alloc = pmm_alloc(size);
    memcpy((void*) ((uintptr_t) boot_trampoline_alloc + g_globals_boot_info->hhdm_offset), (void*) x86_64_kernel_handoff, (x86_64_kernel_handoff_end - x86_64_kernel_handoff));

    uintptr_t current_cr3 = arch_cr_read_cr3();
    uintptr_t level_count = arch_cr_read_cr4() & (1 << 12) ? 5 : 4;
    ptm_map_at(current_cr3, level_count, 0x1000, (uintptr_t) boot_trampoline_alloc, size * PTM_PAGE_GRANULARITY, PTM_FLAG_READ | PTM_FLAG_EXEC);
    ptm_map(0x1000, (uintptr_t) boot_trampoline_alloc, size * PTM_PAGE_GRANULARITY, PTM_FLAG_READ | PTM_FLAG_EXEC);
    g_boot_trampoline = (x86_64_kernel_entry_t) (0x1000);

    ptm_create_hhdm_mappings();

    size_t pmm_map_entries = g_pmm_map_size;
    void* memory_map_block;
    while(true) {
        size_t memory_map_block_size = MATH_ALIGN_UP(pmm_map_entries, PTM_PAGE_GRANULARITY);
        void* memory_map_phys = pmm_alloc(memory_map_block_size / PTM_PAGE_GRANULARITY);
        memory_map_block = (void*) ((uintptr_t) memory_map_phys + boot_info->hhdm_offset);
        if(g_pmm_map_size > pmm_map_entries) {
            pmm_free(memory_map_phys, memory_map_block_size / PTM_PAGE_GRANULARITY);
            pmm_map_entries *= 2;
            log_print("memory map block too small, trying again with %zu entries\n", pmm_map_entries);
        } else {
            break;
        }
    }

    boot_info->mm_entry_count = g_pmm_map_size;
    boot_info->mm_entries = (bootinfo_mm_entry_t*) memory_map_block;
    for(size_t i = 0; i < g_pmm_map_size; i++) {
        pmm_map_entry_t* entry = &g_pmm_map[i];
        bootinfo_mm_type_t type;
        switch(entry->type) {
            case PMM_MAP_TYPE_BOOTLOADER_RECLAIMABLE: [[fallthrough]];
            case PMM_MAP_TYPE_EFI_RECLAIMABLE:        [[fallthrough]];
            case PMM_MAP_TYPE_FREE:                   type = BOOTINFO_MM_TYPE_USABLE; break;
            case PMM_MAP_TYPE_ALLOCATED:              type = BOOTINFO_MM_TYPE_RECLAIMABLE; break;
            case PMM_MAP_TYPE_USED:                   type = BOOTINFO_MM_TYPE_USED; break;
            case PMM_MAP_TYPE_ACPI_RECLAIMABLE:       type = BOOTINFO_MM_TYPE_ACPI_RECLAIMABLE; break;
            case PMM_MAP_TYPE_ACPI_NVS:               type = BOOTINFO_MM_TYPE_ACPI_NVS; break;
            case PMM_MAP_TYPE_RESERVED:               type = BOOTINFO_MM_TYPE_RESERVED; break;
            case PMM_MAP_TYPE_BAD:                    type = BOOTINFO_MM_TYPE_BAD; break;
        }

        boot_info->mm_entries[i].phys_base = entry->base;
        boot_info->mm_entries[i].length = entry->length;
        boot_info->mm_entries[i].type = type;
    }

    arch_machine_init(&start_info_block[0]);

    g_kernel_entry_point = kernel_image_info.entry_point;
    boot_info->kernel_segments = kernel_image_info.segments;
    boot_info->kernel_segment_count = kernel_image_info.segment_count;

    g_core_count = boot_info->core_count;
    ATOMIC_STORE(&g_ap_init_lock, 1, ATOMIC_RELEASE);
    handoff_to_kernel(g_boot_trampoline, g_kernel_entry_point, start_info_block[0].stack, g_ptm.tplt, boot_info, 0);
    while(1);
}
