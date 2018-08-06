// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cinttypes>
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/ns/ns.h"
#include "core/hle/service/ns/ns_s.h"
#include "core/loader/loader.h"

namespace Service {
namespace NS {

Kernel::SharedPtr<Kernel::Process> LaunchTitleImpl(FS::MediaType media_type, u64 title_id) {
    std::string path = AM::GetTitleContentPath(media_type, title_id);
    auto loader = Loader::GetLoader(path);

    if (!loader) {
        LOG_WARNING(Service_NS, "Could not find .app for title 0x{:016X}", title_id);
        return nullptr;
    }

    Kernel::SharedPtr<Kernel::Process> process;
    Loader::ResultStatus result = loader->Load(process);

    if (result != Loader::ResultStatus::Success) {
        LOG_WARNING(Service_NS, "Error loading .app for title 0x{:016X}", title_id);
        return nullptr;
    }

    return process;
}

void NS_S::LaunchTitle(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x2, 3, 0);
    u64 title_id = rp.Pop<u64>();
    u32 flags = rp.Pop<u32>();
    FS::MediaType media_type =
        (title_id == 0) ? FS::MediaType::GameCard : AM::GetTitleMediaType(title_id);
    LOG_WARNING(Service_NS, "(STUBBED) called, title_id={}, media_type={}, flags={}", title_id,
                static_cast<u32>(media_type), flags);
    auto process = LaunchTitleImpl(media_type, title_id);
    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(process ? process->process_id : 0);
}

void NS_S::ShutdownAsync(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0xE, 0, 0);
    Core::System::GetInstance().RequestShutdown();
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(RESULT_SUCCESS);
}

void NS_S::RebootSystemClean(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0xE, 0, 0);
    u64 title_id = Kernel::g_current_process->codeset->program_id;
    FS::MediaType media_type = AM::GetTitleMediaType(title_id);
    Core::System::GetInstance().RequestJump(title_id, media_type);
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(RESULT_SUCCESS);
}

void InstallInterfaces(SM::ServiceManager& service_manager) {
    std::make_shared<NS_S>()->InstallAsService(service_manager);
}

} // namespace NS
} // namespace Service
