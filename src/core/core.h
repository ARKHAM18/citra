// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include "common/common_types.h"
#include "core/hle/applets/erreula.h"
#include "core/hle/applets/swkbd.h"
#include "core/hle/service/fs/archive.h"
#include "core/hle/shared_page.h"
#include "core/loader/loader.h"
#include "core/memory.h"
#include "core/perf_stats.h"

class EmuWindow;
class CPU;

namespace AudioCore {
class DspHle;
} // namespace AudioCore

namespace RPC {
class RPCServer;
} // namespace RPC

namespace Service::SM {
class ServiceManager;
} // namespace Service::SM

namespace Core {

struct QtCallbacks {
    std::function<void(HLE::Applets::ErrEulaConfig&)> erreula;
    std::function<void(HLE::Applets::SoftwareKeyboardConfig&, std::u16string&)> swkbd;
    std::function<void()> update_3d;
};

class System {
public:
    /**
     * Gets the instance of the System singleton class.
     * @returns Reference to the instance of the System singleton class.
     */
    static System& GetInstance() {
        return s_instance;
    }

    /// Enumeration representing the return values of the System Initialize and Load process.
    enum class ResultStatus : u32 {
        Success,                    ///< Succeeded
        ErrorNotInitialized,        ///< Error trying to use core prior to initialization
        ErrorGetLoader,             ///< Error finding the correct application loader
        ErrorSystemMode,            ///< Error determining the system mode
        ErrorLoader,                ///< Error loading the specified application
        ErrorLoader_ErrorEncrypted, ///< Error loading the specified application due to encryption
        ErrorLoader_ErrorInvalidFormat,     ///< Error loading the specified application due to an
                                            /// invalid format
        ErrorSystemFiles,                   ///< Error in finding system files
        ErrorVideoCore,                     ///< Error in the video core
        ErrorVideoCore_ErrorGenericDrivers, ///< Error in the video core due to the user having
                                            /// generic drivers installed
        ErrorVideoCore_ErrorBelowGL33,      ///< Error in the video core due to the user not having
                                            /// OpenGL 3.3 or higher
        ShutdownRequested,                  ///< Emulated program requested a system shutdown
        FatalError
    };

    /**
     * Run the core CPU loop
     * This function runs the core for the specified number of CPU instructions before trying to
     * update hardware. This is much faster than SingleStep (and should be equivalent), as the CPU
     * is not required to do a full dispatch with each instruction. NOTE: the number of instructions
     * requested is not guaranteed to run, as this will be interrupted preemptively if a hardware
     * update is requested (e.g. on a thread switch).
     * @param tight_loop If false, the CPU single-steps.
     * @return Result status, indicating whethor or not the operation succeeded.
     */
    ResultStatus RunLoop();

    /// Shutdown the emulated system.
    void Shutdown();

    /// Shutdown and then load
    void Jump();

    /// Request jump of the system
    void RequestJump(u64 title_id, Service::FS::MediaType media_type) {
        jump_requested = true;
        jump_tid = title_id;
        jump_media = media_type;
    }

    /// Request shutdown of the system
    void RequestShutdown() {
        shutdown_requested = true;
    }

    void SetGame(const std::string& path) {
        shutdown_requested = true;
        file_path = path;
    }

    /**
     * Load an executable application.
     * @param emu_window Reference to the host-system window used for video output and keyboard
     *                   input.
     * @param filepath String path to the executable application to load on the host file system.
     * @returns ResultStatus code, indicating if the operation succeeded.
     */
    ResultStatus Load(EmuWindow& emu_window, const std::string& filepath);
    ResultStatus Load(const std::string& filepath);

    /**
     * Indicates if the emulated system is powered on (all subsystems initialized and able to run an
     * application).
     * @returns True if the emulated system is powered on, otherwise false.
     */
    bool IsPoweredOn() const {
        return cpu_core != nullptr;
    }

    /// Prepare the core emulation for a reschedule
    void PrepareReschedule();

    PerfStats::Results GetAndResetPerfStats();

    /**
     * Gets a reference to the emulated CPU.
     * @returns A reference to the emulated CPU.
     */
    CPU& GetCPU() {
        return *cpu_core;
    }

    /**
     * Gets a reference to the emulated DSP.
     * @returns A reference to the emulated DSP.
     */
    AudioCore::DspHle& DSP() {
        return *dsp_core;
    }

    /**
     * Gets a reference to the service manager.
     * @returns A reference to the service manager.
     */
    Service::SM::ServiceManager& ServiceManager();

    /**
     * Gets a const reference to the service manager.
     * @returns A const reference to the service manager.
     */
    const Service::SM::ServiceManager& ServiceManager() const;

    PerfStats perf_stats;
    FrameLimiter frame_limiter;

    void SetStatus(ResultStatus new_status, const char* details = nullptr) {
        status = new_status;
        if (details) {
            status_details = details;
        }
    }

    const std::string& GetStatusDetails() const {
        return status_details;
    }

    Loader::AppLoader& GetAppLoader() const {
        return *app_loader;
    }

    QtCallbacks& GetQtCallbacks() const {
        return *qt_callbacks;
    }

    std::shared_ptr<SharedPage::Handler> GetSharedPageHandler() const {
        return shared_page_handler;
    }

    bool IsShellOpen() const {
        return shell_open.load(std::memory_order_relaxed);
    }

    void SetShellOpen(bool value) {
        shell_open.store(value, std::memory_order_relaxed);
    }

    void SetRunning(bool running) {
        this->running.store(running, std::memory_order::memory_order_relaxed);
        if (running) {
            std::unique_lock<std::mutex> lock{running_mutex};
            running_cv.notify_one();
        }
    }

    bool IsRunning() {
        return running.load(std::memory_order::memory_order_relaxed);
    }

    std::string file_path;

private:
    /**
     * Initialize the emulated system.
     * @param emu_window Reference to the host-system window used for video output and keyboard
     *                   input.
     * @param system_mode The system mode.
     * @return ResultStatus code, indicating if the operation succeeded.
     */
    ResultStatus Init(EmuWindow& emu_window, u32 system_mode);

    /// Reschedule the core emulation
    void Reschedule();

    /// AppLoader used to load the current executing application
    std::unique_ptr<Loader::AppLoader> app_loader;

    /// Applet factories
    std::unique_ptr<QtCallbacks> qt_callbacks;

    /// ARM11 CPU core
    std::unique_ptr<CPU> cpu_core;

    /// DSP core
    std::unique_ptr<AudioCore::DspHle> dsp_core;

    /// When true, signals that a reschedule should happen
    bool reschedule_pending{};

    /// Service manager
    std::shared_ptr<Service::SM::ServiceManager> service_manager;

    /// Shared page
    std::shared_ptr<SharedPage::Handler> shared_page_handler;

    /// RPC server for scripting
    std::unique_ptr<RPC::RPCServer> rpc_server;

    static System s_instance;

    ResultStatus status;
    std::string status_details;

    EmuWindow* m_emu_window;
    std::string m_filepath;
    u64 jump_tid;
    Service::FS::MediaType jump_media;

    std::atomic<bool> jump_requested;
    std::atomic<bool> shutdown_requested;
    std::atomic<bool> shell_open;
    std::atomic<bool> running;
    std::mutex running_mutex;
    std::condition_variable running_cv;
};

inline CPU& GetCPU() {
    return System::GetInstance().GetCPU();
}

inline AudioCore::DspHle& DSP() {
    return System::GetInstance().DSP();
}

} // namespace Core
