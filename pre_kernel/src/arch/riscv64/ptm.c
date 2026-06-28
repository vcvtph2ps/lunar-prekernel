#include <arch/csr.h>
#include <globals.h>
#include <lib/math.h>
#include <log.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <panic.h>
#include <protocol/bootinfo.h>
#include <runtime/mem.h>
#include <stdint.h>

// @link arch/riscv64/fdt.c
size_t arch_fdt_detect_mmu_levels(bootinfo_t* boot_info);

#define PTE_V ((uint64_t) (1ULL << 0))
#define PTE_R ((uint64_t) (1ULL << 1))
#define PTE_W ((uint64_t) (1ULL << 2))
#define PTE_X ((uint64_t) (1ULL << 3))
#define PTE_G ((uint64_t) (1ULL << 5))
#define PTE_PPN_SHIFT 10
#define PTE_PPN_MASK ((uint64_t) (((uint64_t) 1 << 44) - 1))

#define PA_TO_PPN(pa) (((uint64_t) (uintptr_t) (pa) >> 12) & PTE_PPN_MASK)
#define PTE_TO_PA(pte) ((uintptr_t) (((pte) >> PTE_PPN_SHIFT) << 12))
#define VADDR_VPN(vaddr, level) ((unsigned int) (((uint64_t) (vaddr) >> (12 + 9 * ((int) (level) - 1))) & 0x1FFUL))

#define PAGE_SIZE_AT_LEVEL(level) ((uint64_t) PTM_PAGE_SIZE_4K << (9 * ((int) (level) - 1)))

ptm_t g_ptm = {};
static size_t g_level_count = 3;

static size_t detect_max_level_count() {
    size_t dtb_levels = arch_fdt_detect_mmu_levels(g_globals_boot_info);
    if(dtb_levels == 0) { panic("ptm: failed to detect supported paging modes from DTB"); }
    return dtb_levels;
}


static void map_page(uintptr_t root_pa, size_t total_levels, uint64_t vaddr, uint64_t paddr, int target_level, uint8_t flags) {
    if(target_level < 1 || (size_t) target_level > total_levels) panic("ptm: invalid target_level %d (total=%zu)", target_level, total_levels);

    uint64_t leaf_flags = PTE_V;
    if(flags & PTM_FLAG_READ) leaf_flags |= PTE_R;
    if(flags & PTM_FLAG_WRITE) leaf_flags |= PTE_W;
    if(flags & PTM_FLAG_EXEC) leaf_flags |= PTE_X;

    uint64_t* table = (uint64_t*) (root_pa + g_globals_boot_info->hhdm_offset);

    for(int level = (int) total_levels; level > target_level; level--) {
        unsigned int idx = VADDR_VPN(vaddr, level);
        uint64_t entry = table[idx];

        if(!(entry & PTE_V)) {
            void* child_phys = pmm_alloc(1);
            memset((void*) ((uintptr_t) child_phys + g_globals_boot_info->hhdm_offset), 0, PTM_PAGE_GRANULARITY);
            entry = (PA_TO_PPN(child_phys) << PTE_PPN_SHIFT) | PTE_V;
            table[idx] = entry;
        } else if(entry & (PTE_R | PTE_W | PTE_X)) {
            panic("ptm: walked into existing leaf PTE at level %d (vaddr=0x%lx)", level, vaddr);
        }

        table = (uint64_t*) (PTE_TO_PA(entry) + g_globals_boot_info->hhdm_offset);
    }

    unsigned int idx = VADDR_VPN(vaddr, target_level);
    table[idx] = (PA_TO_PPN(paddr) << PTE_PPN_SHIFT) | leaf_flags;
}

static int choose_target_level(uint64_t vaddr, uint64_t paddr, uint64_t remain, size_t max_levels) {
    for(int level = (int) MATH_MIN(max_levels, 3); level > 1; level--) {
        uint64_t pgsz = PAGE_SIZE_AT_LEVEL(level);
        if(vaddr % pgsz == 0 && paddr % pgsz == 0 && remain >= pgsz) return level;
    }
    return 1;
}

void ptm_init() {
    g_level_count = detect_max_level_count();
    g_ptm.level_count = g_level_count;

    void* root = pmm_alloc(1);
    memset((void*) ((uintptr_t) root + g_globals_boot_info->hhdm_offset), 0, PTM_PAGE_GRANULARITY);
    g_ptm.tplt = (uintptr_t) root;

    log_print("ptm: root_pa=0x%lx  level_count=%zu\n", g_ptm.tplt, g_level_count);
}

void ptm_map(uint64_t vaddr, uint64_t paddr, uint64_t length, uint8_t flags) {
    if(paddr % PTM_PAGE_GRANULARITY || vaddr % PTM_PAGE_GRANULARITY || length % PTM_PAGE_GRANULARITY) panic("ptm_map: unaligned (vaddr=0x%lx paddr=0x%lx len=0x%lx)", vaddr, paddr, length);

    uint64_t offset = 0;
    while(offset < length) {
        int tl = choose_target_level(vaddr, paddr, length - offset, g_level_count);
        uint64_t pgsz = PAGE_SIZE_AT_LEVEL(tl);
        map_page(g_ptm.tplt, g_level_count, vaddr, paddr, tl, flags);
        vaddr += pgsz;
        paddr += pgsz;
        offset += pgsz;
    }
}

void ptm_map_at(uintptr_t root_pa, size_t level_count, uint64_t vaddr, uint64_t paddr, uint64_t length, uint8_t flags) {
    if(paddr % PTM_PAGE_GRANULARITY || vaddr % PTM_PAGE_GRANULARITY || length % PTM_PAGE_GRANULARITY) panic("ptm_map_at: unaligned (vaddr=0x%lx paddr=0x%lx len=0x%lx)", vaddr, paddr, length);

    uint64_t offset = 0;
    while(offset < length) {
        int tl = choose_target_level(vaddr, paddr, length - offset, level_count);
        uint64_t pgsz = PAGE_SIZE_AT_LEVEL(tl);
        map_page(root_pa, level_count, vaddr, paddr, tl, flags);
        vaddr += pgsz;
        paddr += pgsz;
        offset += pgsz;
    }
}

void ptm_create_hhdm_mappings() {
    pmm_map_snapshot_t snapshot = pmm_create_snapshot();

    for(size_t i = 0; i < snapshot.count; i++) {
        switch(snapshot.entries[i].type) {
            case PMM_MAP_TYPE_FREE:
            case PMM_MAP_TYPE_USED:
            case PMM_MAP_TYPE_ALLOCATED:
            case PMM_MAP_TYPE_BOOTLOADER_RECLAIMABLE:
            case PMM_MAP_TYPE_EFI_RECLAIMABLE:
            case PMM_MAP_TYPE_ACPI_RECLAIMABLE:
            case PMM_MAP_TYPE_ACPI_NVS:
            case PMM_MAP_TYPE_RESERVED:               break;
            default:                                  continue;
        }

        uint64_t base = snapshot.entries[i].base;
        uint64_t length = snapshot.entries[i].length;
        if(base % PTM_PAGE_GRANULARITY != 0) {
            length += base % PTM_PAGE_GRANULARITY;
            base -= base % PTM_PAGE_GRANULARITY;
        }
        if(length % PTM_PAGE_GRANULARITY != 0) length += PTM_PAGE_GRANULARITY - length % PTM_PAGE_GRANULARITY;

        ptm_map(g_globals_boot_info->hhdm_offset + base, base, length, PTM_FLAG_READ | PTM_FLAG_WRITE);
    }

    pmm_free_snapshot(&snapshot);
}
