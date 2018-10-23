// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <vector>
#include "core/core.h"
#include "core/hle/kernel/event.h"
#include "core/hle/kernel/shared_memory.h"
#include "core/hle/service/gsp/gsp.h"

namespace Service::GSP {

FrameBufferUpdate* GetFrameBufferInfo(u32 thread_id, u32 screen_index) {
    auto gpu{Core::System::GetInstance().ServiceManager().GetService<GSP_GPU>("gsp::Gpu")};
    ASSERT(gpu != nullptr);
    return gpu->GetFrameBufferInfo(thread_id, screen_index);
}

void SignalInterrupt(InterruptId interrupt_id) {
    auto gpu{Core::System::GetInstance().ServiceManager().GetService<GSP_GPU>("gsp::Gpu")};
    ASSERT(gpu != nullptr);
    return gpu->SignalInterrupt(interrupt_id);
}

void InstallInterfaces(Core::System& system) {
    auto& service_manager{system.ServiceManager()};
    std::make_shared<GSP_GPU>()->InstallAsService(service_manager);
    std::make_shared<GSP_LCD>()->InstallAsService(service_manager);
}

} // namespace Service::GSP
