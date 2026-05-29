#include <arch/cpuid.h>
#include <arch/cr.h>
#include <arch/gdt.h>
#include <arch/msr.h>
#include <log.h>
#include <panic.h>
#include <stdint.h>

#define PAT_UNCACHEABLE 0ULL
#define PAT_WRITE_COMBINING 1ULL
#define PAT_WRITE_THROUGH 4ULL
#define PAT_WRITE_PROTECT 5ULL
#define PAT_WRITE_BACK 6ULL
#define PAT_UNCACHED 7ULL

void setup_page_table_attributes() {
    uint8_t pat0 = PAT_WRITE_BACK;
    uint8_t pat1 = PAT_WRITE_THROUGH;
    uint8_t pat2 = PAT_UNCACHED;
    uint8_t pat3 = PAT_UNCACHEABLE;
    uint8_t pat4 = PAT_WRITE_COMBINING;
    uint8_t pat5 = PAT_UNCACHEABLE; // UNUSED
    uint8_t pat6 = PAT_UNCACHEABLE; // UNUSED
    uint8_t pat7 = PAT_UNCACHEABLE; // UNUSED
    uint64_t pat = pat0 | ((uint64_t) pat1 << 8) | ((uint64_t) pat2 << 16) | ((uint64_t) pat3 << 24) | ((uint64_t) pat4 << 32) | ((uint64_t) pat5 << 40) | ((uint64_t) pat6 << 48) | ((uint64_t) pat7 << 56);
    arch_msr_write(ARCH_MSR_PAT_MSR, pat);
}

static void machine_setup_control_registers(uint64_t core_id) {
    if(!arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_FXSR)) { panic("FXSR not supported on this CPU!"); }

    // PE, MP, ET, WP, PG
    arch_cr_write_cr0(0x80010013);

    uint64_t cr4 = arch_cr_read_cr4();
    bool la57 = cr4 & (1 << 12);

    // PAE, OSFXSR, OSXMMEXCPT
    cr4 = 0x620;
    if(la57) {
        cr4 |= 1 << 12; // CR4.LA57
    }

    bool write_xcr0 = false;
    uint64_t xcr0 = 0;

    log_print("Enabled optional features for core %lu: [", core_id);
    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_UMIP)) {
        cr4 |= (1 << 11); // CR4.UMIP
        log_print_raw("UMIP, ");
    }

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_SMEP)) {
        cr4 |= (1 << 20); // CR4.SMEP
        log_print_raw("SMEP, ");
    }

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_SMAP)) {
        cr4 |= (1 << 21); // CR4.SMAP
        log_print_raw("SMAP, ");
    }

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_FRED)) {
        cr4 |= (1ul << 32); // CR4.FRED
        log_print_raw("FRED, ");
    }

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_PAT)) {
        setup_page_table_attributes();
        log_print_raw("PAT, ");
    }

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_LKGS)) { log_print_raw("LKGS, "); }

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_XSAVE)) {
        log_print_raw("XSAVE, ");
        cr4 |= 1 << 18; // CR4.OSXSAVE

        xcr0 |= 1 << 0; // XCR0.X87
        xcr0 |= 1 << 1; // XCR0.SSE
        if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_AVX)) {
            log_print_raw("AVX, ");
            xcr0 |= 1 << 2; // XCR0.AVX
        }
        if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_AVX512)) {
            log_print_raw("AVX512, ");
            xcr0 |= 1 << 5; // XCR0.opmask
            xcr0 |= 1 << 6; // XCR0.ZMM_Hi256
            xcr0 |= 1 << 7; // XCR0.Hi16_ZMM
        }
    }
    log_print_raw("\b\b]\n");

    arch_cr_write_cr4(cr4);
    if(write_xcr0) { arch_cr_write_xcr0(xcr0); }

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_NX_PAGES)) arch_msr_write(ARCH_MSR_EFER, arch_msr_read(ARCH_MSR_EFER) | (1 << 11)); // EFER.NXE
}

void arch_ldt_load_ldt(uint16_t ldtr);

void machine_setup_gdt() {
    arch_gdt_ptr_t gdtr;
    gdtr.base = (uintptr_t) &g_arch_gdt_static_data;
    gdtr.limit = sizeof(g_arch_gdt_static_data) - 1;

    arch_gdt_load_gdt(&gdtr, __builtin_offsetof(arch_gdt_t, kernel_code), __builtin_offsetof(arch_gdt_t, kernel_data), 0x00);
    arch_ldt_load_ldt(0x00);
}

void arch_machine_init(uint64_t core_id, uintptr_t cpu_local_ptr) {
    machine_setup_control_registers(core_id);
    machine_setup_gdt();
    arch_msr_write(ARCH_MSR_ACTIVE_GS_BASE, cpu_local_ptr);
    arch_msr_write(ARCH_MSR_INACTIVE_GS_BASE, 0);
}
