#pragma once
#include <boot/core.h>
#include <stdint.h>

typedef bool (*boot_core_is_bsp_fn)(uint64_t core_index);
typedef void (*boot_start_ap_fn)(uint64_t core_index, core_start_info_t* boot_info);

extern boot_core_is_bsp_fn g_boot_core_is_bsp;
extern boot_start_ap_fn g_boot_start_ap;
