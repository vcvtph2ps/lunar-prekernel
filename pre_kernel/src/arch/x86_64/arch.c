#include <arch.h>
#include <arch/gdt.h>
#include <boot/boot.h>
#include <globals.h>
#include <lib/helpers.h>
#include <lib/math.h>
#include <log.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <runtime/mem.h>

#define CORE_STACK_PGCNT 16

void arch_init_early() {}

void arch_setup_cpus(core_start_info_t* core_start_info_block, size_t core_count, bootinfo_kernel_info_t* kernel_info) {
    size_t cpu_local_block_size = MATH_ALIGN_UP(core_count * kernel_info->cpu_local_size, PTM_PAGE_GRANULARITY);
    void* cpu_local_block = (void*) ((uintptr_t) pmm_alloc_ext(cpu_local_block_size / PTM_PAGE_GRANULARITY, PTM_PAGE_GRANULARITY, PMM_MAP_TYPE_USED) + g_globals_boot_info->hhdm_offset);
    memset(cpu_local_block, 0, cpu_local_block_size);

    g_globals_boot_info->cpulocal_start = (uintptr_t) cpu_local_block;
    g_globals_boot_info->cpulocal_size = cpu_local_block_size;

    size_t arch_start_info_block_size = MATH_ALIGN_UP(sizeof(arch_core_start_info_t) * core_count, PTM_PAGE_GRANULARITY);
    arch_core_start_info_t* arch_start_info_block = (arch_core_start_info_t*) ((uintptr_t) pmm_alloc(arch_start_info_block_size / PTM_PAGE_GRANULARITY) + g_globals_boot_info->hhdm_offset);
    memset(core_start_info_block, 0, arch_start_info_block_size);

    // @note: setup start info block for bsp here instead of in the loop because core_id
    core_start_info_block[0].core_id = 0;
    core_start_info_block[0].cpu_local = ((uintptr_t) cpu_local_block);
    core_start_info_block[0].stack = ((uintptr_t) pmm_alloc(CORE_STACK_PGCNT) + g_globals_boot_info->hhdm_offset) + (CORE_STACK_PGCNT * PTM_PAGE_GRANULARITY);
    // @note: bsp will fill this in later
    core_start_info_block[0].arch_pointer = (void*) &arch_start_info_block[0];

    uint64_t core_id = 1;
    for(uint64_t i = 0; i < core_count; i++) {
        if(g_boot_core_is_bsp(i)) { continue; }

        core_start_info_t* core_start_info = &core_start_info_block[core_id];
        core_start_info->cpu_local = ((uintptr_t) cpu_local_block) + (core_id * kernel_info->cpu_local_size);
        core_start_info->stack = ((uintptr_t) pmm_alloc(CORE_STACK_PGCNT) + g_globals_boot_info->hhdm_offset) + (CORE_STACK_PGCNT * PTM_PAGE_GRANULARITY);
        core_start_info->core_id = core_id++;

        // @note: we let the aps handle setting up the gdt and tss, we just need to allocate one for them
        core_start_info->arch_pointer = (void*) &arch_start_info_block[core_id];

        log_print("starting ap %lu\n", core_start_info->core_id);
        g_boot_start_ap(i, core_start_info);
    }
}
