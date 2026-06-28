#include <common/libfdt.h>
#include <globals.h>
#include <lib/math.h>
#include <log.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <panic.h>
#include <protocol/bootinfo.h>
#include <runtime/mem.h>

size_t arch_fdt_detect_mmu_levels(bootinfo_t* boot_info) {
    if(!boot_info || boot_info->dtb_physical == 0) return 0;

    const void* fdt = (const void*) (boot_info->dtb_physical + boot_info->hhdm_offset);
    if(fdt_check_header(fdt) != 0) return 0;

    int cpus_node = fdt_path_offset(fdt, "/cpus");
    if(cpus_node < 0) return 0;

    int node;
    fdt_for_each_subnode(node, fdt, cpus_node) {
        const char* devtype = (const char*) fdt_getprop(fdt, node, "device_type", nullptr);
        if(!devtype || strcmp(devtype, "cpu") != 0) { continue; }

        int len = 0;
        const char* mmu = (const char*) fdt_getprop(fdt, node, "mmu-type", &len);
        if(mmu && len > 0) {
            if(strcmp(mmu, "riscv,sv57") == 0) {
                log_print("fdt: mmu-type = \"%s\"\n", mmu);
                return 5;
            }
            if(strcmp(mmu, "riscv,sv48") == 0) {
                log_print("fdt: mmu-type = \"%s\"\n", mmu);
                return 4;
            }
            if(strcmp(mmu, "riscv,sv39") == 0) {
                log_print("fdt: mmu-type = \"%s\"\n", mmu);
                return 3;
            }
            log_print("fdt: unknown mmu-type \"%s\"\n", mmu);
            panic("Unsupported MMU type");
        }

        break;
    }

    log_print("fdt: could not determine MMU level count from DTB\n");
    return 0;
}


bool arch_fdt_parse(bootinfo_t* boot_info) {
    const void* fdt = (const void*) (boot_info->dtb_physical + boot_info->hhdm_offset);

    int rc = fdt_check_header(fdt);
    if(rc != 0) {
        log_print("fdt: invalid FDT header at 0x%lx (err=%d)\n", boot_info->dtb_physical, rc);
        return false;
    }

    log_print("fdt: FDT at phys 0x%lx, size=%d\n", boot_info->dtb_physical, fdt_totalsize(fdt));

    int cpus_node = fdt_path_offset(fdt, "/cpus");
    if(cpus_node < 0) {
        log_print("fdt: /cpus node not found (err=%d)\n", cpus_node);
        return false;
    }

    const char* isa_string = nullptr;
    int isa_len = 0;

    int node;
    fdt_for_each_subnode(node, fdt, cpus_node) {
        const char* compatible = (const char*) fdt_getprop(fdt, node, "device_type", nullptr);
        if(!compatible || strcmp(compatible, "cpu") != 0) { continue; }

        int len = 0;
        const char* isa = (const char*) fdt_getprop(fdt, node, "riscv,isa", &len);
        if(isa && len > 0) {
            isa_string = isa;
            isa_len = len;
            break;
        }
    }

    if(!isa_string) {
        log_print("fdt: riscv,isa property not found in any cpu node\n");
        return false;
    }

    log_print("fdt: riscv,isa = \"%s\"\n", isa_string);

    size_t isa_string_len = 0;

    size_t extention_count = 0;
    for(size_t i = 0; i < (size_t) isa_len; i++) {
        if(isa_string[i] == '_') { extention_count++; }
        if(isa_string[i] == '_' && isa_string_len == 0) { isa_string_len = i; }
    }

    size_t info_size = isa_len + 1;
    info_size += (extention_count + 1) * sizeof(char*);

    info_size = MATH_ALIGN_UP(info_size, PTM_PAGE_GRANULARITY);
    void* info_buffer = (void*) ((uintptr_t) pmm_alloc(info_size / PTM_PAGE_GRANULARITY) + boot_info->hhdm_offset);

    char* cursor = info_buffer;

    char* base_isa = cursor;
    cursor += isa_string_len + 1;
    cursor = (char*) MATH_ALIGN_UP((uintptr_t) cursor, alignof(char*));

    char** extensions = (char**) cursor;
    cursor += (extention_count + 1) * sizeof(char*);

    memcpy(base_isa, isa_string, isa_string_len);
    base_isa[isa_string_len] = '\0';

    size_t ext_index = 0;

    const char* ext_start = isa_string + isa_string_len + 1;
    const char* p = ext_start;

    while(*p) {
        const char* end = p;

        while(*end && *end != '_') { end++; }

        size_t len = (size_t) (end - p);

        extensions[ext_index] = cursor;

        memcpy(cursor, p, len);
        cursor[len] = '\0';

        cursor += len + 1;
        ext_index++;

        if(*end == '\0') { break; }

        p = end + 1;
    }

    boot_info->riscv_base_isa_string = base_isa;
    boot_info->riscv_extension_count = ext_index;
    boot_info->riscv_extentions = (char*(*) []) extensions;

    return true;
}
