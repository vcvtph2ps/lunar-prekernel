#include <arch/16550uart.h>
#include <arch/cr.h>
#include <arch/gdt.h>
#include <arch/machine.h>
#include <arch/msr.h>
#include <boot/ap.h>
#include <boot/boot.h>
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

#define CORE_STACK_PGCNT 16

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

[[noreturn]] void prekernel_init_ap(ap_boot_info_t* boot_info) {
    while(ATOMIC_LOAD(&g_ap_init_lock, ATOMIC_ACQUIRE) == 0);

    arch_machine_init(boot_info->core_id, (uintptr_t) boot_info->cpu_local, boot_info->gdt_pointer);

    handoff_to_kernel(g_boot_trampoline, g_kernel_entry_point, boot_info->ap_stack, g_ptm.tplt, nullptr, boot_info->core_id);
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

    uintptr_t stack = ((uintptr_t) pmm_alloc(CORE_STACK_PGCNT) + boot_info->hhdm_offset) + (CORE_STACK_PGCNT * PTM_PAGE_GRANULARITY);

    size_t system_page_count = MATH_ALIGN_UP(boot_info->hhdm_size, PTM_PAGE_GRANULARITY) / PTM_PAGE_GRANULARITY;

    uintptr_t pfndb_start;
    size_t pfndb_size;
    pagedb_setup(kernel_image_info.kernel_base, kernel_image_info.kernel_info->pagedb_entry_size, &pfndb_start, &pfndb_size);
    boot_info->pfndb_start = pfndb_start;
    boot_info->pfndb_size = pfndb_size;

    size_t cpu_local_block_size = MATH_ALIGN_UP(system_page_count * kernel_image_info.kernel_info->cpu_local_size, PTM_PAGE_GRANULARITY);
    void* cpu_local_block = (void*) ((uintptr_t) pmm_alloc_ext(cpu_local_block_size / PTM_PAGE_GRANULARITY, PTM_PAGE_GRANULARITY, PMM_MAP_TYPE_USED) + boot_info->hhdm_offset);
    log_print("cpu_local size: %zu\n", kernel_image_info.kernel_info->cpu_local_size);
    memset(cpu_local_block, 0, cpu_local_block_size);

    size_t gdt_block_size = MATH_ALIGN_UP(sizeof(arch_gdt_block_t) * boot_info->core_count, PTM_PAGE_GRANULARITY);
    arch_gdt_block_t* gdt_blocks = (arch_gdt_block_t*) ((uintptr_t) pmm_alloc(gdt_block_size / PTM_PAGE_GRANULARITY) + boot_info->hhdm_offset);
    memset(gdt_blocks, 0, gdt_block_size);

    void* ap_boot_info_block = (void*) ((uintptr_t) pmm_alloc(MATH_ALIGN_UP(sizeof(ap_boot_info_t) * boot_info->core_count, PTM_PAGE_GRANULARITY) / PTM_PAGE_GRANULARITY) + boot_info->hhdm_offset);

    boot_info->cpulocal_start = (uintptr_t) cpu_local_block;
    boot_info->cpulocal_size = cpu_local_block_size;

    uint64_t core_id = 1;
    for(uint64_t i = 0; i < boot_info->core_count; i++) {
        if(g_boot_core_is_bsp(i)) { continue; }

        log_print("starting ap %lu\n", i);

        ap_boot_info_t* ap_boot_info = &((ap_boot_info_t*) ap_boot_info_block)[i];
        ap_boot_info->cpu_local = ((uintptr_t) cpu_local_block) + (core_id * kernel_image_info.kernel_info->cpu_local_size);
        ap_boot_info->ap_stack = ((uintptr_t) pmm_alloc(CORE_STACK_PGCNT) + boot_info->hhdm_offset) + (CORE_STACK_PGCNT * PTM_PAGE_GRANULARITY);
        ap_boot_info->gdt_pointer = (uintptr_t) &gdt_blocks[core_id];
        ap_boot_info->core_id = core_id++;
        g_boot_start_ap(i, ap_boot_info);
    }

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

    arch_machine_init(0, (uintptr_t) cpu_local_block, (uintptr_t) &gdt_blocks[0]);

    g_kernel_entry_point = kernel_image_info.entry_point;
    boot_info->kernel_segments = kernel_image_info.segments;
    boot_info->kernel_segment_count = kernel_image_info.segment_count;

    g_core_count = boot_info->core_count;
    ATOMIC_STORE(&g_ap_init_lock, 1, ATOMIC_RELEASE);

    handoff_to_kernel(g_boot_trampoline, g_kernel_entry_point, stack, g_ptm.tplt, boot_info, 0);
    while(1);
}
