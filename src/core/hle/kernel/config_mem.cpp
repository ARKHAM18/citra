// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include "core/hle/kernel/config_mem.h"

namespace ConfigMem {

Handler::Handler() {
    std::memset(&config_mem, 0, sizeof(config_mem));
    // TODO: Update this
    // Values extracted from firmware 11.2.0-35E
    config_mem.kernel_version_min = 0x34;
    config_mem.kernel_version_maj = 0x2;
    config_mem.ns_program_id = 0x0004013000008002;
    config_mem.sys_core_ver = 0x2;
    config_mem.unit_info = 0x1; // Bit 0 set for Retail
    config_mem.prev_firm = 0x1;
    config_mem.ctr_sdk_ver = 0x0000F297;
    config_mem.firm_version_min = 0x34;
    config_mem.firm_version_maj = 0x2;
    config_mem.firm_sys_core_ver = 0x2;
    config_mem.firm_ctr_sdk_ver = 0x0000F297;
}

ConfigMemDef& Handler::GetConfigMem() {
    return config_mem;
}

} // namespace ConfigMem
