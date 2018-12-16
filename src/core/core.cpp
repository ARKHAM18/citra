// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <utility>
#include "audio_core/hle/hle.h"
#include "audio_core/lle/lle.h"
#include "common/logging/log.h"
#include "core/cheats/cheats.h"
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
#include "core/movie.h"
#include "network/room.h"
#include "network/room_member.h"
#ifdef ENABLE_SCRIPTING
#include "core/rpc/rpc_server.h"
#endif
#include "core/settings.h"
#include "video_core/renderer/renderer.h"
#include "video_core/video_core.h"

namespace Core {

System System::s_instance;

void System::Init1() {
    room = std::make_unique<Network::Room>();
    room_member = std::make_unique<Network::RoomMember>();
    movie = std::make_unique<Movie>(*this);
}

System::~System() {
    if (room_member) {
        if (room_member->IsConnected())
            room_member->Leave();
        room_member.reset();
    }
    if (room) {
        if (room->IsOpen())
            room->Destroy();
        room.reset();
    }
    movie.reset();
}

System::ResultStatus System::Run() {
    status = ResultStatus::Success;
    if (!cpu_core)
        return ResultStatus::ErrorNotInitialized;
    if (!running.load(std::memory_order::memory_order_relaxed)) {
        std::unique_lock lock{running_mutex};
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
    if (!kernel->GetThreadManager().GetCurrentThread()) {
        LOG_TRACE(Core_ARM11, "Idling");
        timing->Idle();
        timing->Advance();
        PrepareReschedule();
    } else {
        timing->Advance();
        cpu_core->Run();
    }
    HW::Update();
    Reschedule();
    if (shutdown_requested.exchange(false))
        return ResultStatus::ShutdownRequested;
    return status;
}

System::ResultStatus System::Load(Frontend& frontend, const std::string& filepath) {
    program_loader = Loader::GetLoader(*this, filepath);
    if (!program_loader) {
        LOG_ERROR(Core, "Failed to obtain loader for {}!", filepath);
        return ResultStatus::ErrorGetLoader;
    }
    std::pair<std::optional<u32>, Loader::ResultStatus> system_mode{
        program_loader->LoadKernelSystemMode()};
    if (system_mode.second != Loader::ResultStatus::Success) {
        LOG_ERROR(Core, "Failed to determine system mode (Error {})!",
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
    auto init_result{Init(frontend, *system_mode.first)};
    if (init_result != ResultStatus::Success) {
        LOG_ERROR(Core, "Failed to initialize system (Error {})!", static_cast<u32>(init_result));
        Shutdown();
        return init_result;
    }
    Kernel::SharedPtr<Kernel::Process> process;
    const auto load_result{program_loader->Load(process)};
    kernel->SetCurrentProcess(process);
    if (Loader::ResultStatus::Success != load_result) {
        LOG_ERROR(Core, "Failed to load file (Error {})!", static_cast<u32>(load_result));
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
    memory->SetCurrentPageTable(&kernel->GetCurrentProcess()->vm_manager.page_table);
    cheat_engine = std::make_unique<Cheats::CheatEngine>(*this);
    status = ResultStatus::Success;
    m_filepath = filepath;
    Settings::Apply(*this);
    return status;
}

void System::PrepareReschedule() {
    cpu_core->PrepareReschedule();
    reschedule_pending = true;
}

PerfStats::Results System::GetAndResetPerfStats() {
    return perf_stats.GetAndResetStats(timing->GetGlobalTimeUs());
}

void System::Reschedule() {
    if (!reschedule_pending)
        return;
    reschedule_pending = false;
    kernel->GetThreadManager().Reschedule();
}

System::ResultStatus System::Init(Frontend& frontend, u32 system_mode) {
    m_frontend = &frontend;
    memory = std::make_unique<Memory::MemorySystem>(*this);
    LOG_DEBUG(HW_Memory, "initialized OK");
    timing = std::make_unique<Core::Timing>();
    kernel = std::make_unique<Kernel::KernelSystem>(*this);
    // Initialize FS, CFG and memory
    service_manager = std::make_unique<Service::SM::ServiceManager>(*this);
    archive_manager = std::make_unique<Service::FS::ArchiveManager>(*this);
    Service::FS::InstallInterfaces(*this);
    Service::CFG::InstallInterfaces(*this);
    kernel->MemoryInit(system_mode);
    cpu_core = std::make_unique<Cpu>(*this);
    if (Settings::values.use_lle_dsp)
        dsp_core = std::make_unique<AudioCore::DspLle>(*this);
    else
        dsp_core = std::make_unique<AudioCore::DspHle>(*this);
    dsp_core->EnableStretching(Settings::values.enable_audio_stretching);
#ifdef ENABLE_SCRIPTING
    rpc_server = std::make_unique<RPC::RPCServer>(*this);
#endif
    shutdown_requested = false;
    sleep_mode_enabled = false;
    HW::Init();
    Service::Init(*this);
    auto result{VideoCore::Init(*this)};
    if (result != ResultStatus::Success)
        return result;
    LOG_DEBUG(Core, "Initialized OK");
    // Reset counters and set time origin to current frame
    GetAndResetPerfStats();
    perf_stats.BeginSystemFrame();
    return ResultStatus::Success;
}

const Service::SM::ServiceManager& System::ServiceManager() const {
    return *service_manager;
}

Service::SM::ServiceManager& System::ServiceManager() {
    return *service_manager;
}

const Service::FS::ArchiveManager& System::ArchiveManager() const {
    return *archive_manager;
}

Service::FS::ArchiveManager& System::ArchiveManager() {
    return *archive_manager;
}

const Kernel::KernelSystem& System::Kernel() const {
    return *kernel;
}

Kernel::KernelSystem& System::Kernel() {
    return *kernel;
}

const Cheats::CheatEngine& System::CheatEngine() const {
    return *cheat_engine;
}

Cheats::CheatEngine& System::CheatEngine() {
    return *cheat_engine;
}

const Timing& System::CoreTiming() const {
    return *timing;
}

Timing& System::CoreTiming() {
    return *timing;
}

const Network::Room& System::Room() const {
    return *room;
}

Network::Room& System::Room() {
    return *room;
}

const Network::RoomMember& System::RoomMember() const {
    return *room_member;
}

Network::RoomMember& System::RoomMember() {
    return *room_member;
}

const Movie& System::MovieSystem() const {
    return *movie;
}

Movie& System::MovieSystem() {
    return *movie;
}

const Memory::MemorySystem& System::Memory() const {
    return *memory;
}

Memory::MemorySystem& System::Memory() {
    return *memory;
}

const Frontend& System::GetFrontend() const {
    return *m_frontend;
}

Frontend& System::GetFrontend() {
    return *m_frontend;
}

void System::Shutdown() {
    // Shutdown emulation session
    cpu_core.reset();
    cheat_engine.reset();
    VideoCore::Shutdown();
    kernel.reset();
    HW::Shutdown();
#ifdef ENABLE_SCRIPTING
    rpc_server.reset();
#endif
    service_manager.reset();
    dsp_core.reset();
    timing.reset();
    program_loader.reset();
    memory.reset();
    room_member->SetProgram(std::string{});
    LOG_DEBUG(Core, "Shutdown OK");
}

void System::Restart() {
    SetProgram(m_filepath);
}

void System::SetProgram(const std::string& path) {
    shutdown_requested = true;
    set_program_file_path = path;
}

void System::CloseProgram() {
    SetProgram("");
}

} // namespace Core
