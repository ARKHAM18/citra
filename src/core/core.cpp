// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <utility>
#include "audio_core/hle/hle.h"
#include "common/logging/log.h"
#include "core/cheat_core.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/cpu/cpu.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/service/cfg/cfg.h"
#include "core/hle/service/fs/archive.h"
#include "core/hle/service/fs/fs_user.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"
#include "core/hw/hw.h"
#include "core/loader/loader.h"
#include "core/memory_setup.h"
#include "core/movie.h"
#ifdef ENABLE_SCRIPTING
#include "core/rpc/rpc_server.h"
#endif
#ifdef ENABLE_WEB_SERVICE
#include "web_service/verify_login.h"
#endif
#include "core/settings.h"
#include "network/network.h"
#include "video_core/renderer/renderer.h"
#include "video_core/video_core.h"

namespace Core {

/*static*/ System System::s_instance;

System::ResultStatus System::RunLoop() {
    status = ResultStatus::Success;
    if (!cpu_core)
        return ResultStatus::ErrorNotInitialized;
    if (!running.load(std::memory_order::memory_order_relaxed)) {
        std::unique_lock<std::mutex> lock{running_mutex};
        running_cv.wait(lock);
    }
    if (!dsp_core->IsOutputAllowed()) {
        // Draw black screens to the emulator window
        VideoCore::g_renderer->SwapBuffers();
        // Sleep for one frame or the PC would overheat
        std::this_thread::sleep_for(std::chrono::milliseconds{16});
        return ResultStatus::Success;
    }
    // If we don't have a currently active thread then don't execute instructions,
    // instead advance to the next event and try to yield to the next thread
    if (!Kernel::GetCurrentThread()) {
        LOG_TRACE(Core_ARM11, "Idling");
        CoreTiming::Idle();
        CoreTiming::Advance();
        PrepareReschedule();
    } else {
        CoreTiming::Advance();
        cpu_core->Run();
    }
    HW::Update();
    Reschedule();
    if (shutdown_requested.exchange(false))
        return ResultStatus::ShutdownRequested;
    return status;
}

System::ResultStatus System::Load(Frontend& frontend, const std::string& filepath) {
    app_loader = Loader::GetLoader(filepath);
    if (!app_loader) {
        LOG_CRITICAL(Core, "Failed to obtain loader for {}!", filepath);
        return ResultStatus::ErrorGetLoader;
    }
    std::pair<std::optional<u32>, Loader::ResultStatus> system_mode{
        app_loader->LoadKernelSystemMode()};
    if (system_mode.second != Loader::ResultStatus::Success) {
        LOG_CRITICAL(Core, "Failed to determine system mode (Error {})!",
                     static_cast<int>(system_mode.second));
        switch (system_mode.second) {
        case Loader::ResultStatus::ErrorEncrypted:
            return ResultStatus::ErrorLoader_ErrorEncrypted;
        case Loader::ResultStatus::ErrorInvalidFormat:
            return ResultStatus::ErrorLoader_ErrorInvalidFormat;
        default:
            return ResultStatus::ErrorSystemMode;
        }
    }
    ASSERT(system_mode.first);
    ResultStatus init_result{Init(frontend, *system_mode.first)};
    if (init_result != ResultStatus::Success) {
        LOG_CRITICAL(Core, "Failed to initialize system (Error {})!",
                     static_cast<u32>(init_result));
        Shutdown();
        return init_result;
    }
    Kernel::SharedPtr<Kernel::Process> process;
    const Loader::ResultStatus load_result{app_loader->Load(process)};
    kernel->SetCurrentProcess(process);
    if (Loader::ResultStatus::Success != load_result) {
        LOG_CRITICAL(Core, "Failed to load ROM (Error {})!", static_cast<u32>(load_result));
        Shutdown();
        switch (load_result) {
        case Loader::ResultStatus::ErrorEncrypted:
            return ResultStatus::ErrorLoader_ErrorEncrypted;
        case Loader::ResultStatus::ErrorInvalidFormat:
            return ResultStatus::ErrorLoader_ErrorInvalidFormat;
        default:
            return ResultStatus::ErrorLoader;
        }
    }
    Memory::SetCurrentPageTable(&kernel->GetCurrentProcess()->vm_manager.page_table);
    status = ResultStatus::Success;
    m_filepath = filepath;
    return status;
}

void System::PrepareReschedule() {
    cpu_core->PrepareReschedule();
    reschedule_pending = true;
}

PerfStats::Results System::GetAndResetPerfStats() {
    return perf_stats.GetAndResetStats(CoreTiming::GetGlobalTimeUs());
}

void System::Reschedule() {
    if (!reschedule_pending)
        return;
    reschedule_pending = false;
    Kernel::Reschedule();
}

System::ResultStatus System::Init(Frontend& frontend, u32 system_mode) {
    LOG_DEBUG(HW_Memory, "initialized OK");
    CoreTiming::Init();
    kernel = std::make_unique<Kernel::KernelSystem>(system_mode);
    cpu_core = std::make_unique<Cpu>();
    dsp_core = std::make_unique<AudioCore::DspHle>();
    dsp_core->EnableStretching(Settings::values.enable_audio_stretching);
#ifdef ENABLE_SCRIPTING
    rpc_server = std::make_unique<RPC::RPCServer>();
#endif
    service_manager = std::make_shared<Service::SM::ServiceManager>(*this);
    shared_page_handler = std::make_shared<SharedPage::Handler>();
    archive_manager = std::make_unique<Service::FS::ArchiveManager>(*this);
    shutdown_requested = false;
    sleep_mode_enabled = false;
    // Initialize FS and CFG
    Service::FS::InstallInterfaces(*this);
    Service::CFG::InstallInterfaces(*this);
    HW::Init();
    Service::Init(*this);
    CheatCore::Init();
    ResultStatus result{VideoCore::Init(frontend)};
    if (result != ResultStatus::Success)
        return result;
    LOG_DEBUG(Core, "Initialized OK");
    // Reset counters and set time origin to current frame
    GetAndResetPerfStats();
    perf_stats.BeginSystemFrame();
    SetRunning(true);
    return ResultStatus::Success;
}

Service::SM::ServiceManager& System::ServiceManager() {
    return *service_manager;
}

const Service::SM::ServiceManager& System::ServiceManager() const {
    return *service_manager;
}

Service::FS::ArchiveManager& System::ArchiveManager() {
    return *archive_manager;
}

const Service::FS::ArchiveManager& System::ArchiveManager() const {
    return *archive_manager;
}

Kernel::KernelSystem& System::Kernel() {
    return *kernel;
}

const Kernel::KernelSystem& System::Kernel() const {
    return *kernel;
}

const Frontend& System::GetFrontend() const {
    return *m_frontend;
}

void System::Shutdown() {
    // Shutdown emulation session
    CheatCore::Shutdown();
    VideoCore::Shutdown();
    kernel.reset();
    HW::Shutdown();
#ifdef ENABLE_SCRIPTING
    rpc_server.reset();
#endif
    service_manager.reset();
    dsp_core.reset();
    cpu_core.reset();
    CoreTiming::Shutdown();
    app_loader.reset();
    if (auto member{Network::GetRoomMember().lock()}) {
        Network::GameInfo game_info{};
        member->SendGameInfo(game_info);
    }
    LOG_DEBUG(Core, "Shutdown OK");
}

void System::Restart() {
    SetApplication(m_filepath);
}

void System::SetApplication(const std::string& path) {
    shutdown_requested = true;
    set_application_file_path = path;
}

void System::CloseApplication() {
    SetApplication("");
}

bool VerifyLogin(const std::string& host, const std::string& username, const std::string& token) {
#ifdef ENABLE_WEB_SERVICE
    return WebService::VerifyLogin(host, username, token);
#else
    return false;
#endif
}

} // namespace Core
