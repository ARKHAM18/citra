// Co 'tdoesn' tdoesn 'tdoesn' tator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifdef _MSC_VER
#include <intrin.h>
#endif
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <clocale>
#include <memory>
#include <thread>
#include <glad/glad.h>
#define QT_NO_OPENGL
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <QtGui>
#include <QtWidgets>
#ifdef ENABLE_DISCORD_RPC
#include <discord_rpc.h>
#endif
#include <fmt/format.h>
#include "citra/aboutdialog.h"
#include "citra/app_list.h"
#include "citra/bootmanager.h"
#include "citra/camera/qt_multimedia_camera.h"
#include "citra/camera/still_image_camera.h"
#include "citra/cheats.h"
#include "citra/configuration/config.h"
#include "citra/configuration/configure_dialog.h"
#include "citra/control_panel.h"
#include "citra/hotkeys.h"
#include "citra/main.h"
#include "citra/mii_selector.h"
#include "citra/multiplayer/state.h"
#include "citra/swkbd.h"
#include "citra/ui_settings.h"
#include "citra/util/clickable_label.h"
#include "citra/util/util.h"
#include "common/common_paths.h"
#include "common/detached_tasks.h"
#include "common/logging/backend.h"
#include "common/logging/filter.h"
#include "common/logging/log.h"
#include "common/logging/text_formatter.h"
#include "common/scm_rev.h"
#include "common/scope_exit.h"
#include "core/3ds.h"
#include "core/core.h"
#include "core/file_sys/archive_extsavedata.h"
#include "core/file_sys/archive_source_sd_savedata.h"
#include "core/file_sys/seed_db.h"
#include "core/hle/kernel/shared_page.h"
#include "core/hle/service/am/am_u.h"
#include "core/hle/service/cfg/cfg.h"
#include "core/hle/service/fs/archive.h"
#include "core/hle/service/nfc/nfc.h"
#include "core/hle/service/nwm/nwm_ext.h"
#include "core/hle/service/ptm/ptm.h"
#include "core/loader/loader.h"
#include "core/movie.h"
#include "core/rpc/rpc_server.h"
#include "core/settings.h"
#include "video_core/renderer/renderer.h"
#include "video_core/video_core.h"

#ifdef QT_STATICPLUGIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin);
#endif

#ifdef _WIN32
extern "C" {
// tells Nvidia drivers to use the dedicated GPU by default on laptops with switchable graphics
__declspec(dllexport) unsigned long NvOptimusEnablement{0x00000001};
}
#endif

#ifdef ENABLE_DISCORD_RPC
static void HandleDiscordDisconnected(int error_code, const char* message) {
    LOG_ERROR(Frontend, "Discord RPC disconnected ({} {})", error_code, message);
}

static void HandleDiscordError(int error_code, const char* message) {
    LOG_ERROR(Frontend, "Discord RPC error ({} {})", error_code, message);
}
#endif

const int GMainWindow::max_recent_files_item;

static void InitializeLogging() {
    Log::Filter log_filter;
    log_filter.ParseFilterString(Settings::values.log_filter);
    Log::SetGlobalFilter(log_filter);
    Log::AddBackend(std::make_unique<Log::FileBackend>(
        FileUtil::GetUserPath(FileUtil::UserPath::UserDir) + LOG_FILE));
}

GMainWindow::GMainWindow() : config{new Config()} {
    InitializeLogging();
    ToggleConsole();
    Settings::LogSettings();
    // Register types to use in slots and signals
    qRegisterMetaType<std::size_t>("std::size_t");
    qRegisterMetaType<Service::AM::InstallStatus>("Service::AM::InstallStatus");
    setAcceptDrops(true);
    ui.setupUi(this);
    statusBar()->hide();
    default_theme_paths = QIcon::themeSearchPaths();
    UpdateUITheme();
    InitializeWidgets();
    InitializeRecentFileMenuActions();
    InitializeHotkeys();
    SetDefaultUIGeometry();
    RestoreUIState();
    ConnectMenuEvents();
    ConnectWidgetEvents();
    UpdateTitle();
#ifdef _MSC_VER
    int cpu_id[4];
    __cpuid(cpu_id, 1);
    if (!((cpu_id[2] >> 19) & 1)) {
        QMessageBox::critical(this, "Citra", "Your CPU doesn't support SSE4.1");
        closeEvent(nullptr);
    }
#endif
    app_list->PopulateAsync(UISettings::values.app_dirs);
    QStringList args{QApplication::arguments()};
    if (args.length() >= 2)
        BootApplication(args[1].toStdString());
    if (UISettings::values.enable_discord_rpc)
        InitializeDiscordRPC();
}

GMainWindow::~GMainWindow() {
    // Will get automatically deleted otherwise
    if (!screens->parent())
        delete screens;
#ifdef ENABLE_DISCORD_RPC
    if (UISettings::values.enable_discord_rpc)
        ShutdownDiscordRPC();
#endif
}

void GMainWindow::InitializeWidgets() {
    screens = new Screens(this, emu_thread.get());
    screens->hide();

    app_list = new AppList(this);
    ui.horizontalLayout->addWidget(app_list);

    app_list_placeholder = new AppListPlaceholder(this);
    ui.horizontalLayout->addWidget(app_list_placeholder);
    app_list_placeholder->setVisible(false);

    multiplayer_state = new MultiplayerState(this, app_list->GetModel(), ui.action_Leave_Room,
                                             ui.action_Show_Room, system);
    multiplayer_state->setVisible(false);

    // Create status bar
    message_label = new QLabel();

    // Configured separately for left alignment
    message_label->setVisible(false);
    message_label->setFrameStyle(QFrame::NoFrame);
    message_label->setContentsMargins(4, 0, 4, 0);
    message_label->setAlignment(Qt::AlignLeft);
    statusBar()->addPermanentWidget(message_label, 1);

    progress_bar = new QProgressBar();
    progress_bar->setMaximum(INT_MAX);
    progress_bar->hide();
    statusBar()->addPermanentWidget(progress_bar);

    touch_label = new QLabel();
    touch_label->hide();

    perf_stats_label = new QLabel();
    perf_stats_label->hide();
    perf_stats_label->setToolTip("Performance information (Speed | FPS | Frametime)");
    perf_stats_label->setFrameStyle(QFrame::NoFrame);
    perf_stats_label->setContentsMargins(4, 0, 4, 0);

    statusBar()->addPermanentWidget(touch_label, 0);
    statusBar()->addPermanentWidget(perf_stats_label, 0);
    statusBar()->addPermanentWidget(multiplayer_state->GetStatusIcon(), 0);
    statusBar()->setVisible(true);

    // Removes an ugly inner border from the status bar widgets under Linux
    setStyleSheet("QStatusBar::item{border: none;}");

    QActionGroup* actionGroup_ScreenLayouts{new QActionGroup(this)};
    actionGroup_ScreenLayouts->addAction(ui.action_Screen_Layout_Default);
    actionGroup_ScreenLayouts->addAction(ui.action_Screen_Layout_Single_Screen);
    actionGroup_ScreenLayouts->addAction(ui.action_Screen_Layout_Medium_Screen);
    actionGroup_ScreenLayouts->addAction(ui.action_Screen_Layout_Large_Screen);
    actionGroup_ScreenLayouts->addAction(ui.action_Screen_Layout_Side_by_Side);

    QActionGroup* actionGroup_NAND{new QActionGroup(this)};
    actionGroup_NAND->addAction(ui.action_NAND_Default);
    actionGroup_NAND->addAction(ui.action_NAND_Custom);

    QActionGroup* actionGroup_SDMC{new QActionGroup(this)};
    actionGroup_SDMC->addAction(ui.action_SDMC_Default);
    actionGroup_SDMC->addAction(ui.action_SDMC_Custom);
}

void GMainWindow::InitializeRecentFileMenuActions() {
    for (int i{}; i < max_recent_files_item; ++i) {
        actions_recent_files[i] = new QAction(this);
        actions_recent_files[i]->setVisible(false);
        connect(actions_recent_files[i], &QAction::triggered, this, &GMainWindow::OnMenuRecentFile);
        ui.menu_recent_files->addAction(actions_recent_files[i]);
    }
    ui.menu_recent_files->addSeparator();
    QAction* action_clear_recent_files{new QAction(this)};
    action_clear_recent_files->setText("Clear Recent Files");
    connect(action_clear_recent_files, &QAction::triggered, this, [this] {
        UISettings::values.recent_files.clear();
        UpdateRecentFiles();
    });
    ui.menu_recent_files->addAction(action_clear_recent_files);
    UpdateRecentFiles();
}

void GMainWindow::InitializeHotkeys() {
    hotkey_registry.RegisterHotkey("Main Window", "Load File", QKeySequence::Open);
    hotkey_registry.RegisterHotkey("Main Window", "Load/Remove Amiibo",
                                   QKeySequence(Qt::Key_Comma));
    hotkey_registry.RegisterHotkey("Main Window", "Continue/Pause", QKeySequence(Qt::Key_F4));
    hotkey_registry.RegisterHotkey("Main Window", "Restart", QKeySequence(Qt::Key_F5));
    hotkey_registry.RegisterHotkey("Main Window", "Swap Screens", QKeySequence(Qt::Key_F9));
    hotkey_registry.RegisterHotkey("Main Window", "Toggle Screen Layout",
                                   QKeySequence(Qt::Key_F10));
    hotkey_registry.RegisterHotkey("Main Window", "Fullscreen", QKeySequence::FullScreen);
    hotkey_registry.RegisterHotkey("Main Window", "Exit Fullscreen", QKeySequence(Qt::Key_Escape));
    hotkey_registry.RegisterHotkey("Main Window", "Toggle Speed Limit", QKeySequence("CTRL+Z"));
    hotkey_registry.RegisterHotkey("Main Window", "Increase Speed Limit",
                                   QKeySequence(Qt::Key_Plus));
    hotkey_registry.RegisterHotkey("Main Window", "Decrease Speed Limit",
                                   QKeySequence(Qt::Key_Minus));
    hotkey_registry.RegisterHotkey("Main Window", "Increase Internal Resolution",
                                   QKeySequence("CTRL+I"));
    hotkey_registry.RegisterHotkey("Main Window", "Decrease Internal Resolution",
                                   QKeySequence("CTRL+D"));
    hotkey_registry.RegisterHotkey("Main Window", "Capture Screenshot", QKeySequence("CTRL+S"));
    hotkey_registry.RegisterHotkey("Main Window", "Toggle Sleep Mode", QKeySequence("F2"));
    hotkey_registry.RegisterHotkey("Main Window", "Change CPU Ticks", QKeySequence("CTRL+T"));
    hotkey_registry.RegisterHotkey("Main Window", "Toggle Frame Advancing", QKeySequence("CTRL+A"));
    hotkey_registry.RegisterHotkey("Main Window", "Advance Frame", QKeySequence(Qt::Key_Backslash));
    hotkey_registry.RegisterHotkey("Main Window", "Open User Directory", QKeySequence("CTRL+U"));
    hotkey_registry.RegisterHotkey("Main Window", "Toggle Hardware Shaders",
                                   QKeySequence("CTRL+W"));
    hotkey_registry.LoadHotkeys();
    connect(hotkey_registry.GetHotkey("Main Window", "Load File", this), &QShortcut::activated,
            this, &GMainWindow::OnMenuLoadFile);
    connect(hotkey_registry.GetHotkey("Main Window", "Load/Remove Amiibo", this),
            &QShortcut::activated, this, [&] {
                if (!system.IsPoweredOn())
                    return;
                if (ui.action_Remove_Amiibo->isEnabled())
                    OnRemoveAmiibo();
                else
                    OnLoadAmiibo();
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Continue/Pause", this), &QShortcut::activated,
            this, [&] {
                if (system.IsPoweredOn()) {
                    if (system.IsRunning())
                        OnPauseApplication();
                    else
                        OnStartApplication();
                }
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Restart", this), &QShortcut::activated, this,
            [this] {
                if (!system.IsPoweredOn())
                    return;
                BootApplication(system.GetFilePath());
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Swap Screens", screens),
            &QShortcut::activated, ui.action_Screen_Layout_Swap_Screens, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Toggle Screen Layout", screens),
            &QShortcut::activated, this, &GMainWindow::ToggleScreenLayout);
    connect(hotkey_registry.GetHotkey("Main Window", "Fullscreen", screens), &QShortcut::activated,
            ui.action_Fullscreen, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Fullscreen", screens),
            &QShortcut::activatedAmbiguously, ui.action_Fullscreen, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Exit Fullscreen", this),
            &QShortcut::activated, this, [&] {
                if (system.IsPoweredOn()) {
                    ui.action_Fullscreen->setChecked(false);
                    ToggleFullscreen();
                }
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Toggle Speed Limit", this),
            &QShortcut::activated, this, [&] {
                Settings::values.use_frame_limit = !Settings::values.use_frame_limit;
                UpdatePerformanceStats();
            });
    constexpr u16 SPEED_LIMIT_STEP{5};
    connect(hotkey_registry.GetHotkey("Main Window", "Increase Speed Limit", this),
            &QShortcut::activated, this, [&] {
                if (Settings::values.frame_limit < 9999 - SPEED_LIMIT_STEP) {
                    Settings::values.frame_limit += SPEED_LIMIT_STEP;
                    UpdatePerformanceStats();
                }
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Decrease Speed Limit", this),
            &QShortcut::activated, this, [&] {
                if (Settings::values.frame_limit > SPEED_LIMIT_STEP) {
                    Settings::values.frame_limit -= SPEED_LIMIT_STEP;
                    UpdatePerformanceStats();
                }
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Increase Internal Resolution", this),
            &QShortcut::activated, this, [&] {
                if (Settings::values.resolution_factor < 10)
                    ++Settings::values.resolution_factor;
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Decrease Internal Resolution", this),
            &QShortcut::activated, this, [&] {
                if (Settings::values.resolution_factor > 1)
                    --Settings::values.resolution_factor;
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Capture Screenshot", this),
            &QShortcut::activated, this, [&] {
                if (system.IsRunning())
                    OnCaptureScreenshot();
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Toggle Sleep Mode", this),
            &QShortcut::activated, this, [&] {
                if (system.IsPoweredOn()) {
                    bool new_value{!system.IsSleepModeEnabled()};
                    system.SetSleepModeEnabled(new_value);
                    ui.action_Sleep_Mode->setChecked(new_value);
                }
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Change CPU Ticks", this),
            &QShortcut::activated, this, [&] {
                QString str{QInputDialog::getText(this, "Change CPU Ticks", "Ticks:")};
                if (str.isEmpty())
                    return;
                bool ok{};
                u64 ticks{str.toULongLong(&ok)};
                if (ok) {
                    Settings::values.ticks_mode = Settings::TicksMode::Custom;
                    Settings::values.ticks = ticks;
                    if (system.IsPoweredOn())
                        system.CPU().SyncSettings();
                } else
                    QMessageBox::critical(this, "Error", "Invalid number");
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Toggle Frame Advancing", this),
            &QShortcut::activated, ui.action_Enable_Frame_Advancing, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Advance Frame", this), &QShortcut::activated,
            ui.action_Advance_Frame, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Open User Directory", this),
            &QShortcut::activated, ui.action_Open_User_Directory, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Toggle Hardware Shaders", this),
            &QShortcut::activated, this, [&] {
                Settings::values.use_hw_shaders = !Settings::values.use_hw_shaders;
                Settings::Apply();
            });
}

void GMainWindow::SetDefaultUIGeometry() {
    // geometry: 55% of the window contents are in the upper screen half, 45% in the lower half
    const QRect screenRect{QApplication::desktop()->screenGeometry(this)};
    const int w{screenRect.width() * 2 / 3};
    const int h{screenRect.height() / 2};
    const int x{(screenRect.x() + screenRect.width()) / 2 - w / 2};
    const int y{(screenRect.y() + screenRect.height()) / 2 - h * 55 / 100};
    setGeometry(x, y, w, h);
}

void GMainWindow::RestoreUIState() {
    restoreGeometry(UISettings::values.geometry);
    restoreState(UISettings::values.state);
    screens->restoreGeometry(UISettings::values.screens_geometry);
    app_list->LoadInterfaceLayout();
    screens->BackupGeometry();
    ui.horizontalLayout->addWidget(screens);
    screens->setFocusPolicy(Qt::ClickFocus);
    ui.action_Fullscreen->setChecked(UISettings::values.fullscreen);
    SyncMenuUISettings();
    ui.action_Show_Filter_Bar->setChecked(UISettings::values.show_filter_bar);
    app_list->setFilterVisible(ui.action_Show_Filter_Bar->isChecked());
    ui.action_Show_Status_Bar->setChecked(UISettings::values.show_status_bar);
    statusBar()->setVisible(ui.action_Show_Status_Bar->isChecked());
    ui.action_NAND_Default->setChecked(Settings::values.nand_dir.empty());
    ui.action_NAND_Custom->setChecked(!Settings::values.nand_dir.empty());
    ui.action_SDMC_Default->setChecked(Settings::values.sdmc_dir.empty());
    ui.action_SDMC_Custom->setChecked(!Settings::values.sdmc_dir.empty());
}

void GMainWindow::ConnectWidgetEvents() {
    connect(app_list, &AppList::ApplicationChosen, this, &GMainWindow::OnAppListLoadFile);
    connect(app_list, &AppList::OpenDirectory, this, &GMainWindow::OnAppListOpenDirectory);
    connect(app_list, &AppList::OpenFolderRequested, this, &GMainWindow::OnAppListOpenFolder);
    connect(app_list, &AppList::AddDirectory, this, &GMainWindow::OnAppListAddDirectory);
    connect(app_list_placeholder, &AppListPlaceholder::AddDirectory, this,
            &GMainWindow::OnAppListAddDirectory);
    connect(app_list, &AppList::ShowList, this, &GMainWindow::OnAppListShowList);
    connect(this, &GMainWindow::EmulationStarting, screens, &Screens::OnEmulationStarting);
    connect(this, &GMainWindow::EmulationStopping, screens, &Screens::OnEmulationStopping);
    connect(&perf_stats_update_timer, &QTimer::timeout, this, &GMainWindow::UpdatePerformanceStats);
    connect(this, &GMainWindow::UpdateProgress, this, &GMainWindow::OnUpdateProgress);
    connect(this, &GMainWindow::CIAInstallReport, this, &GMainWindow::OnCIAInstallReport);
    connect(this, &GMainWindow::CIAInstallFinished, this, &GMainWindow::OnCIAInstallFinished);
    connect(this, &GMainWindow::UpdateThemedIcons, multiplayer_state,
            &MultiplayerState::UpdateThemedIcons);
}

void GMainWindow::ConnectMenuEvents() {
    // File
    connect(ui.action_Load_File, &QAction::triggered, this, &GMainWindow::OnMenuLoadFile);
    connect(ui.action_Install_CIA, &QAction::triggered, this, &GMainWindow::OnMenuInstallCIA);
    connect(ui.action_Add_Seed, &QAction::triggered, this, &GMainWindow::OnMenuAddSeed);
    connect(ui.action_Exit, &QAction::triggered, this, &QMainWindow::close);
    connect(ui.action_Open_User_Directory, &QAction::triggered, this,
            &GMainWindow::OnOpenUserDirectory);
    connect(ui.action_Load_Amiibo, &QAction::triggered, this, &GMainWindow::OnLoadAmiibo);
    connect(ui.action_Remove_Amiibo, &QAction::triggered, this, &GMainWindow::OnRemoveAmiibo);
    connect(ui.action_NAND_Default, &QAction::triggered, this, &GMainWindow::OnNANDDefault);
    connect(ui.action_NAND_Custom, &QAction::triggered, this, &GMainWindow::OnNANDCustom);
    connect(ui.action_SDMC_Default, &QAction::triggered, this, &GMainWindow::OnSDMCDefault);
    connect(ui.action_SDMC_Custom, &QAction::triggered, this, &GMainWindow::OnSDMCCustom);

    // Emulation
    connect(ui.action_Start, &QAction::triggered, this, &GMainWindow::OnStartApplication);
    connect(ui.action_Pause, &QAction::triggered, this, &GMainWindow::OnPauseApplication);
    connect(ui.action_Stop, &QAction::triggered, this, &GMainWindow::OnStopApplication);
    connect(ui.action_Restart, &QAction::triggered, this,
            [this] { BootApplication(system.GetFilePath()); });
    connect(ui.action_Sleep_Mode, &QAction::triggered, this, &GMainWindow::ToggleSleepMode);
    connect(ui.action_Configuration, &QAction::triggered, this, &GMainWindow::OnOpenConfiguration);
    connect(ui.action_Cheats, &QAction::triggered, this, &GMainWindow::OnCheats);
    connect(ui.action_Control_Panel, &QAction::triggered, this, &GMainWindow::OnControlPanel);

    // View
    ui.action_Show_Filter_Bar->setShortcut(QKeySequence("CTRL+F"));
    connect(ui.action_Show_Filter_Bar, &QAction::triggered, this, &GMainWindow::OnToggleFilterBar);
    connect(ui.action_Show_Status_Bar, &QAction::triggered, statusBar(), &QStatusBar::setVisible);
    ui.action_Fullscreen->setShortcut(
        hotkey_registry.GetHotkey("Main Window", "Fullscreen", this)->key());
    ui.action_Screen_Layout_Swap_Screens->setShortcut(
        hotkey_registry.GetHotkey("Main Window", "Swap Screens", this)->key());
    ui.action_Screen_Layout_Swap_Screens->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(ui.action_Fullscreen, &QAction::triggered, this, &GMainWindow::ToggleFullscreen);
    connect(ui.action_Screen_Layout_Default, &QAction::triggered, this,
            &GMainWindow::ChangeScreenLayout);
    connect(ui.action_Screen_Layout_Single_Screen, &QAction::triggered, this,
            &GMainWindow::ChangeScreenLayout);
    connect(ui.action_Screen_Layout_Medium_Screen, &QAction::triggered, this,
            &GMainWindow::ChangeScreenLayout);
    connect(ui.action_Screen_Layout_Large_Screen, &QAction::triggered, this,
            &GMainWindow::ChangeScreenLayout);
    connect(ui.action_Screen_Layout_Side_by_Side, &QAction::triggered, this,
            &GMainWindow::ChangeScreenLayout);
    connect(ui.action_Screen_Layout_Swap_Screens, &QAction::triggered, this,
            &GMainWindow::OnSwapScreens);

    // Tools
    connect(ui.action_Record_Movie, &QAction::triggered, this, &GMainWindow::OnRecordMovie);
    connect(ui.action_Play_Movie, &QAction::triggered, this, &GMainWindow::OnPlayMovie);
    connect(ui.action_Stop_Recording_Playback, &QAction::triggered, this,
            &GMainWindow::OnStopRecordingPlayback);
    connect(ui.action_Capture_Screenshot, &QAction::triggered, this,
            &GMainWindow::OnCaptureScreenshot);
    connect(ui.action_Set_Play_Coins, &QAction::triggered, this, &GMainWindow::OnSetPlayCoins);
    connect(ui.action_Enable_Frame_Advancing, &QAction::triggered, this, [this] {
        if (system.IsPoweredOn()) {
            system.frame_limiter.SetFrameAdvancing(ui.action_Enable_Frame_Advancing->isChecked());
            ui.action_Advance_Frame->setEnabled(ui.action_Enable_Frame_Advancing->isChecked());
        }
    });
    connect(ui.action_Advance_Frame, &QAction::triggered, this, [this] {
        if (system.IsPoweredOn()) {
            ui.action_Enable_Frame_Advancing->setChecked(true);
            ui.action_Advance_Frame->setEnabled(true);
            system.frame_limiter.AdvanceFrame();
        }
    });

    // Multiplayer
    connect(ui.action_View_Lobby, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnViewLobby);
    connect(ui.action_Start_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnCreateRoom);
    connect(ui.action_Leave_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnCloseRoom);
    connect(ui.action_Connect_To_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnDirectConnectToRoom);
    connect(ui.action_Show_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnOpenNetworkRoom);

    // Help
    connect(ui.action_About, &QAction::triggered, this, &GMainWindow::OnMenuAboutCitra);
}

bool GMainWindow::LoadROM(const std::string& filename) {
    // Shutdown previous session if the emu thread is still active...
    if (emu_thread)
        ShutdownApplication();
    screens->InitRenderTarget();
    screens->MakeCurrent();
    if (!gladLoadGL()) {
        QMessageBox::critical(this, "OpenGL 3.3 Unsupported",
                              "Your GPU may not support OpenGL 3.3, or you don't "
                              "have the latest graphics driver.");
        return false;
    }
    const Core::System::ResultStatus result{system.Load(*screens, filename)};
    if (result != Core::System::ResultStatus::Success) {
        switch (result) {
        case Core::System::ResultStatus::ErrorGetLoader:
            LOG_ERROR(Frontend, "Failed to obtain loader for {}!", filename);
            QMessageBox::critical(
                this, "Invalid ROM Format",
                "Your ROM format isn't supported.<br/>Please follow the guides to redump your "
                "<a "
                "href='https://github.com/citra-valentin/citra/wiki/"
                "Dumping-Game-Cartridges/'>game "
                "cartridges</a> or "
                "<a "
                "href='https://github.com/citra-valentin/citra/wiki/"
                "Dumping-Installed-Titles/'>installed "
                "titles</a>.");
            break;
        case Core::System::ResultStatus::ErrorSystemMode:
            LOG_ERROR(Frontend, "Failed to load ROM!");
            QMessageBox::critical(
                this, "ROM Corrupted",
                "Your ROM is corrupted. <br/>Please follow the guides to redump your "
                "<a "
                "href='https://github.com/citra-valentin/citra/wiki/"
                "Dumping-Game-Cartridges/'>game "
                "cartridges</a> or "
                "<a "
                "href='https://github.com/citra-valentin/citra/wiki/"
                "Dumping-Installed-Titles/'>installed "
                "titles</a>.");
            break;
        case Core::System::ResultStatus::ErrorLoader_ErrorEncrypted:
            QMessageBox::critical(
                this, "ROM Encrypted",
                "Your ROM is encrypted. <br/>Please follow the guides to redump your "
                "<a "
                "href='https://github.com/citra-valentin/citra/wiki/"
                "Dumping-Game-Cartridges/'>game "
                "cartridges</a> or "
                "<a "
                "href='https://github.com/citra-valentin/citra/wiki/"
                "Dumping-Installed-Titles/'>installed "
                "titles</a>.");
            break;
        case Core::System::ResultStatus::ErrorLoader_ErrorInvalidFormat:
            QMessageBox::critical(
                this, "Invalid ROM Format",
                "Your ROM format isn't supported.<br/>Please follow the guides to redump your "
                "<a "
                "href='https://github.com/citra-valentin/citra/wiki/"
                "Dumping-Game-Cartridges/'>game "
                "cartridges</a> or "
                "<a "
                "href='https://github.com/citra-valentin/citra/wiki/"
                "Dumping-Installed-Titles/'>installed "
                "titles</a>.");
            break;
        case Core::System::ResultStatus::ErrorVideoCore:
            QMessageBox::critical(
                this, "Video Core Error",
                "An error has occured. Please see the log for more details.<br/>Ensure that you "
                "have the latest graphics drivers for your GPU.");
            break;
        case Core::System::ResultStatus::ErrorVideoCore_ErrorGenericDrivers:
            QMessageBox::critical(
                this, "Video Core Error",
                "You are running default Windows drivers "
                "for your GPU. You need to install the "
                "proper drivers for your graphics card from the manufacturer's website.");
            break;
        case Core::System::ResultStatus::ErrorVideoCore_ErrorBelowGL33:
            QMessageBox::critical(this, "OpenGL 3.3 Unsupported",
                                  "Your GPU may not support OpenGL 3.3, or you don't "
                                  "have the latest graphics driver.");
            break;
        default:
            QMessageBox::critical(this, "Error while loading ROM!",
                                  "An unknown error occured. Please see the log for more details.");
            break;
        }
        return false;
    }
    system.GetAppLoader().ReadShortTitle(short_title);
    UpdateTitle();
    if (cheats_window)
        cheats_window->UpdateTitleID();
    else
        system.CheatManager().RefreshCheats();
    return true;
}

void GMainWindow::BootApplication(const std::string& filename) {
    LOG_INFO(Frontend, "Booting {}", filename);
    StoreRecentFile(QString::fromStdString(filename)); // Put the filename on top of the list
    if (movie_record_on_start)
        Core::Movie::GetInstance().PrepareForRecording();
    if (!LoadROM(filename))
        return;

    // Create and start the emulation thread
    emu_thread = std::make_unique<EmuThread>(screens);
    emit EmulationStarting(emu_thread.get());
    screens->moveContext();
    emu_thread->start();

    connect(screens, &Screens::Closed, this, &GMainWindow::OnStopApplication);
    connect(screens, &Screens::TouchChanged, this, &GMainWindow::OnTouchChanged);

    // Update the GUI
    app_list->hide();
    app_list_placeholder->hide();
    ui.action_Sleep_Mode->setEnabled(true);
    ui.action_Sleep_Mode->setChecked(false);

    perf_stats_update_timer.start(2000);
    screens->show();
    screens->setFocus();
    if (ui.action_Fullscreen->isChecked())
        ShowFullscreen();
    OnStartApplication();
    HLE::Applets::ErrEula::cb = [this](HLE::Applets::ErrEulaConfig& config, bool& open) {
        ErrEulaCallback(config, open);
    };
    HLE::Applets::SoftwareKeyboard::cb = [this](HLE::Applets::SoftwareKeyboardConfig& config,
                                                std::u16string& text,
                                                bool& open) { SwkbdCallback(config, text, open); };
    HLE::Applets::MiiSelector::cb = [this](const HLE::Applets::MiiConfig& config,
                                           HLE::Applets::MiiResult& result, bool& open) {
        MiiSelectorCallback(config, result, open);
    };
    SharedPage::Handler::update_3d = [this] { Update3D(); };
    RPC::RPCServer::cb_update_frame_advancing = [this] { UpdateFrameAdvancingCallback(); };
    Service::NWM::NWM_EXT::update_control_panel = [this] { UpdateControlPanelNetwork(); };
    // Update Discord RPC
    auto& member{system.RoomMember()};
    if (!member.IsConnected())
        UpdateDiscordRPC(member.GetRoomInformation());
}

void GMainWindow::ShutdownApplication() {
    OnStopRecordingPlayback();
    emu_thread->RequestStop(system);

    // Frame advancing must be cancelled in order to release the emu thread from waiting
    system.frame_limiter.SetFrameAdvancing(false);

    emit EmulationStopping();

    // Wait for emulation thread to complete and delete it
    emu_thread->wait();
    emu_thread = nullptr;

    Camera::QtMultimediaCameraHandler::ReleaseHandlers();

    // The emulation is stopped, so closing the window or not doesn't matter anymore
    disconnect(screens, &Screens::Closed, this, &GMainWindow::OnStopApplication);

    // Update the GUI
    ui.action_Start->setEnabled(false);
    ui.action_Start->setText("Start");
    ui.action_Pause->setEnabled(false);
    ui.action_Stop->setEnabled(false);
    ui.action_Restart->setEnabled(false);
    ui.action_Cheats->setEnabled(false);
    ui.action_NAND_Default->setEnabled(true);
    ui.action_NAND_Custom->setEnabled(true);
    ui.action_SDMC_Default->setEnabled(true);
    ui.action_SDMC_Custom->setEnabled(true);
    ui.action_Capture_Screenshot->setEnabled(false);
    ui.action_Load_Amiibo->setEnabled(false);
    ui.action_Remove_Amiibo->setEnabled(false);
    ui.action_Enable_Frame_Advancing->setEnabled(false);
    ui.action_Enable_Frame_Advancing->setChecked(false);
    ui.action_Advance_Frame->setEnabled(false);
    ui.action_Sleep_Mode->setEnabled(false);
    ui.action_Sleep_Mode->setChecked(false);
    screens->hide();
    if (app_list->isEmpty())
        app_list_placeholder->show();
    else
        app_list->show();
    app_list->setFilterFocus();

    // Disable status bar updates
    perf_stats_update_timer.stop();
    message_label->setVisible(false);
    perf_stats_label->setVisible(false);
    touch_label->setVisible(false);

    short_title.clear();
    UpdateTitle();
    // Update Discord RPC
    auto& member{system.RoomMember()};
    if (!member.IsConnected())
        UpdateDiscordRPC(member.GetRoomInformation());
}

void GMainWindow::StoreRecentFile(const QString& filename) {
    UISettings::values.recent_files.prepend(filename);
    UISettings::values.recent_files.removeDuplicates();
    while (UISettings::values.recent_files.size() > max_recent_files_item)
        UISettings::values.recent_files.removeLast();
    UpdateRecentFiles();
}

void GMainWindow::ErrEulaCallback(HLE::Applets::ErrEulaConfig& config, bool& open) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "ErrEulaCallback", Qt::BlockingQueuedConnection,
                                  Q_ARG(HLE::Applets::ErrEulaConfig&, config), Q_ARG(bool&, open));
        return;
    }
    switch (config.error_type) {
    case HLE::Applets::ErrEulaErrorType::ErrorCode:
        QMessageBox::critical(nullptr, "ErrEula",
                              QString::fromStdString(fmt::format("0x{:08X}", config.error_code)));
        break;
    case HLE::Applets::ErrEulaErrorType::LocalizedErrorText:
    case HLE::Applets::ErrEulaErrorType::ErrorText:
        QMessageBox::critical(
            nullptr, "ErrEula",
            QString::fromStdString(
                fmt::format("0x{:08X}\n{}", config.error_code,
                            Common::UTF16ToUTF8(std::u16string(
                                reinterpret_cast<const char16_t*>(config.error_text.data()))))));
        break;
    case HLE::Applets::ErrEulaErrorType::Agree:
    case HLE::Applets::ErrEulaErrorType::Eula:
    case HLE::Applets::ErrEulaErrorType::EulaDrawOnly:
    case HLE::Applets::ErrEulaErrorType::EulaFirstBoot:
        if (QMessageBox::question(nullptr, "ErrEula", "Agree EULA?") ==
            QMessageBox::StandardButton::Yes)
            Core::System::GetInstance()
                .ServiceManager()
                .GetService<Service::CFG::Module::Interface>("cfg:u")
                ->GetModule()
                ->AgreeEula();
        break;
    }
    config.return_code = HLE::Applets::ErrEulaResult::Success;
    open = false;
}

void GMainWindow::SwkbdCallback(HLE::Applets::SoftwareKeyboardConfig& config, std::u16string& text,
                                bool& open) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "SwkbdCallback", Qt::BlockingQueuedConnection,
                                  Q_ARG(HLE::Applets::SoftwareKeyboardConfig&, config),
                                  Q_ARG(std::u16string&, text), Q_ARG(bool&, open));
        return;
    }
    SoftwareKeyboardDialog dialog{this, config, text};
    dialog.exec();
    open = false;
}

void GMainWindow::MiiSelectorCallback(const HLE::Applets::MiiConfig& config,
                                      HLE::Applets::MiiResult& result, bool& open) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "MiiSelectorCallback", Qt::BlockingQueuedConnection,
                                  Q_ARG(const HLE::Applets::MiiConfig&, config),
                                  Q_ARG(HLE::Applets::MiiResult&, result), Q_ARG(bool&, open));
        return;
    }
    MiiSelectorDialog dialog{this, config, result};
    dialog.exec();
    open = false;
}

void GMainWindow::Update3D() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "Update3D", Qt::BlockingQueuedConnection);
        return;
    }
    if (control_panel)
        control_panel->Update3D();
}

void GMainWindow::UpdateFrameAdvancingCallback() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "UpdateFrameAdvancingCallback",
                                  Qt::BlockingQueuedConnection);
        return;
    }
    const bool enabled{system.frame_limiter.GetFrameAdvancing()};
    ui.action_Enable_Frame_Advancing->setChecked(enabled);
    ui.action_Advance_Frame->setEnabled(enabled);
}

void GMainWindow::UpdateControlPanelNetwork() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "UpdateControlPanelNetwork", Qt::BlockingQueuedConnection);
        return;
    }
    if (control_panel)
        control_panel->UpdateNetwork();
}

void GMainWindow::UpdateRecentFiles() {
    const int num_recent_files{
        std::min(UISettings::values.recent_files.size(), max_recent_files_item)};
    for (int i{}; i < num_recent_files; i++) {
        const QString text{QString("%1. %2").arg(i + 1).arg(
            QFileInfo(UISettings::values.recent_files[i]).fileName())};
        actions_recent_files[i]->setText(text);
        actions_recent_files[i]->setData(UISettings::values.recent_files[i]);
        actions_recent_files[i]->setToolTip(UISettings::values.recent_files[i]);
        actions_recent_files[i]->setVisible(true);
    }
    for (int j{num_recent_files}; j < max_recent_files_item; ++j)
        actions_recent_files[j]->setVisible(false);
    // Enable the recent files menu if the list isn't empty
    ui.menu_recent_files->setEnabled(num_recent_files != 0);
}

void GMainWindow::OnAppListLoadFile(const QString& path) {
    BootApplication(path.toStdString());
}

void GMainWindow::OnAppListOpenFolder(u64 data_id, AppListOpenTarget target) {
    std::string path, open_target;
    switch (target) {
    case AppListOpenTarget::SaveData:
        open_target = "Save Data";
        path = FileSys::ArchiveSource_SDSaveData::GetSaveDataPathFor(
            FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir, Settings::values.sdmc_dir + "/"),
            data_id);
        break;
    case AppListOpenTarget::ExtData:
        open_target = "Extra Data";
        path = FileSys::GetExtDataPathFromId(
            FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir, Settings::values.sdmc_dir + "/"),
            data_id);
        break;
    case AppListOpenTarget::Application:
        open_target = "Application";
        path = Service::AM::GetTitlePath(Service::AM::GetTitleMediaType(data_id), data_id) +
               "content/";
        break;
    case AppListOpenTarget::UpdateData:
        open_target = "Update Data";
        path = Service::AM::GetTitlePath(Service::FS::MediaType::SDMC, data_id + 0xe00000000) +
               "content/";
        break;
    default:
        LOG_ERROR(Frontend, "Unexpected target {}", static_cast<int>(target));
        return;
    }
    QString qpath{QString::fromStdString(path)};
    QDir dir{qpath};
    if (!dir.exists()) {
        QMessageBox::critical(
            this, QString("Error Opening %1 Folder").arg(QString::fromStdString(open_target)),
            "Folder doesn't exist!");
        return;
    }

    LOG_INFO(Frontend, "Opening {} path for data_id={:016x}", open_target, data_id);

    QDesktopServices::openUrl(QUrl::fromLocalFile(qpath));
}

void GMainWindow::OnAppListOpenDirectory(const QString& directory) {
    QString path;
    if (directory == "INSTALLED")
        path = QString::fromStdString(
            FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir, Settings::values.sdmc_dir + "/") +
            "Nintendo "
            "3DS/00000000000000000000000000000000/00000000000000000000000000000000/title/00040000");
    else if (directory == "SYSTEM")
        path = QString::fromStdString(
            FileUtil::GetUserPath(FileUtil::UserPath::NANDDir, Settings::values.nand_dir + "/")
                .c_str() +
            std::string("00000000000000000000000000000000/title/00040010"));
    else
        path = directory;
    if (!QFileInfo::exists(path)) {
        QMessageBox::critical(this, QString("Error Opening %1").arg(path), "Folder doesn't exist!");
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void GMainWindow::OnAppListAddDirectory() {
    QString dir_path{QFileDialog::getExistingDirectory(this, "Select Directory")};
    if (dir_path.isEmpty())
        return;
    UISettings::AppDir app_dir{dir_path, false, true};
    if (!UISettings::values.app_dirs.contains(app_dir)) {
        UISettings::values.app_dirs.append(app_dir);
        app_list->PopulateAsync(UISettings::values.app_dirs);
    } else
        LOG_WARNING(Frontend, "Selected directory is already in the application list");
}

void GMainWindow::OnAppListShowList(bool show) {
    if (system.IsPoweredOn())
        return;
    app_list->setVisible(show);
    app_list_placeholder->setVisible(!show);
}

void GMainWindow::OnMenuLoadFile() {
    const QString extensions{QString("*.").append(AppList::supported_file_extensions.join(" *."))};
    const QString file_filter{QString("3DS Executable (%1);;All Files (*.*)").arg(extensions)};
    const QString filename{QFileDialog::getOpenFileName(this, "Load File", ".", file_filter)};
    if (filename.isEmpty())
        return;
    BootApplication(filename.toStdString());
}

void GMainWindow::OnMenuInstallCIA() {
    QStringList filepaths{QFileDialog::getOpenFileNames(
        this, "Install CIA", ".", "CTR Importable Archive (*.cia);;All Files (*.*)")};
    if (filepaths.isEmpty())
        return;

    ui.action_Install_CIA->setEnabled(false);
    app_list->setDirectoryWatcherEnabled(false);
    progress_bar->show();

    QtConcurrent::run([&, filepaths] {
        Service::AM::InstallStatus status;
        const auto cia_progress{
            [&](std::size_t written, std::size_t total) { emit UpdateProgress(written, total); }};
        for (const auto& current_path : filepaths) {
            status = Service::AM::InstallCIA(current_path.toStdString(), cia_progress);
            emit CIAInstallReport(status, current_path);
        }
        emit CIAInstallFinished();
    });
}

void GMainWindow::OnMenuAddSeed() {
    const QString filepath{QFileDialog::getOpenFileName(this, "Add Seed", ".")};
    if (filepath.isEmpty())
        return;
    const QString program_id_s{QInputDialog::getText(this, "Add Seed", "Enter the program ID")};
    if (program_id_s.isEmpty())
        return;
    bool ok{};
    u64 program_id{program_id_s.toULongLong(&ok, 16)};
    if (!ok) {
        QMessageBox::critical(this, "Error", "Invalid program ID");
        return;
    }
    FileSys::Seed seed{};
    seed.title_id = program_id;
    FileUtil::IOFile file{filepath.toStdString(), "rb"};
    file.ReadBytes(seed.data.data(), seed.data.size());
    FileSys::SeedDB db;
    db.Load();
    db.Add(seed);
    db.Save();
    app_list->PopulateAsync(UISettings::values.app_dirs);
}

void GMainWindow::OnUpdateProgress(std::size_t written, std::size_t total) {
    progress_bar->setValue(
        static_cast<int>(INT_MAX * (static_cast<double>(written) / static_cast<double>(total))));
}

void GMainWindow::OnCIAInstallReport(Service::AM::InstallStatus status, const QString& filepath) {
    const QString filename{QFileInfo{filepath}.fileName()};
    switch (status) {
    case Service::AM::InstallStatus::Success:
        statusBar()->showMessage(QString("%1 installed").arg(filename));
        break;
    case Service::AM::InstallStatus::ErrorFailedToOpenFile:
        QMessageBox::critical(this, "Unable to open file",
                              QString("Could not open %1").arg(filename));
        break;
    case Service::AM::InstallStatus::ErrorAborted:
        QMessageBox::critical(
            this, "Installation aborted",
            QString("The installation of %1 was aborted. Please see the log for more details")
                .arg(filename));
        break;
    case Service::AM::InstallStatus::ErrorInvalid:
        QMessageBox::critical(this, "Invalid File", QString("%1 isn't a valid CIA").arg(filename));
        break;
    case Service::AM::InstallStatus::ErrorEncrypted:
        QMessageBox::critical(this, "Encrypted File",
                              QString("%1 must be decrypted "
                                      "before being used with Citra. A real console is required.")
                                  .arg(filename));
        break;
    }
}

void GMainWindow::OnCIAInstallFinished() {
    progress_bar->hide();
    progress_bar->setValue(0);
    app_list->setDirectoryWatcherEnabled(true);
    ui.action_Install_CIA->setEnabled(true);
    app_list->PopulateAsync(UISettings::values.app_dirs);
    if (system.IsPoweredOn()) {
        auto am{system.ServiceManager().GetService<Service::AM::AM_U>("am:u")->GetModule()};
        am->ScanForAllTitles();
    }
}

void GMainWindow::OnMenuRecentFile() {
    QAction* action{qobject_cast<QAction*>(sender())};
    ASSERT(action);
    const QString filename{action->data().toString()};
    if (QFileInfo::exists(filename))
        BootApplication(filename.toStdString());
    else {
        // Display an error message and remove the file from the list.
        QMessageBox::information(this, "File not found",
                                 QString("File \"%1\" not found").arg(filename));
        UISettings::values.recent_files.removeOne(filename);
        UpdateRecentFiles();
    }
}

void GMainWindow::OnStartApplication() {
    Camera::QtMultimediaCameraHandler::ResumeCameras();
    if (movie_record_on_start) {
        Core::Movie::GetInstance().StartRecording(movie_record_path.toStdString());
        movie_record_on_start = false;
        movie_record_path.clear();
    }
    qRegisterMetaType<Core::System::ResultStatus>("Core::System::ResultStatus");
    qRegisterMetaType<std::string>("std::string");
    connect(emu_thread.get(), &EmuThread::ErrorThrown, this, &GMainWindow::OnCoreError);
    system.SetRunning(true);
    ui.action_Start->setEnabled(false);
    ui.action_Start->setText("Continue");
    ui.action_Pause->setEnabled(true);
    ui.action_Stop->setEnabled(true);
    ui.action_Restart->setEnabled(true);
    ui.action_Cheats->setEnabled(true);
    ui.action_NAND_Default->setEnabled(false);
    ui.action_NAND_Custom->setEnabled(false);
    ui.action_SDMC_Default->setEnabled(false);
    ui.action_SDMC_Custom->setEnabled(false);
    ui.action_Capture_Screenshot->setEnabled(true);
    ui.action_Load_Amiibo->setEnabled(true);
    ui.action_Enable_Frame_Advancing->setEnabled(true);
}

void GMainWindow::OnPauseApplication() {
    system.SetRunning(false);
    Camera::QtMultimediaCameraHandler::StopCameras();
    ui.action_Start->setEnabled(true);
    ui.action_Pause->setEnabled(false);
    ui.action_Stop->setEnabled(true);
}

void GMainWindow::OnStopApplication() {
    ShutdownApplication();
    if (cheats_window)
        cheats_window->close();
}

void GMainWindow::OnTouchChanged(unsigned x, unsigned y) {
    touch_label->setText(QString("Touch: %1, %2").arg(QString::number(x), QString::number(y)));
    touch_label->setVisible(true);
}

void GMainWindow::ToggleFullscreen() {
    if (!system.IsPoweredOn())
        return;
    if (ui.action_Fullscreen->isChecked())
        ShowFullscreen();
    else
        HideFullscreen();
}

void GMainWindow::ShowFullscreen() {
    UISettings::values.geometry = saveGeometry();
    ui.menubar->hide();
    statusBar()->hide();
    showFullScreen();
}

void GMainWindow::HideFullscreen() {
    statusBar()->setVisible(ui.action_Show_Status_Bar->isChecked());
    ui.menubar->show();
    showNormal();
    restoreGeometry(UISettings::values.geometry);
}

void GMainWindow::ChangeScreenLayout() {
    Settings::LayoutOption new_layout{Settings::LayoutOption::Default};
    if (ui.action_Screen_Layout_Default->isChecked())
        new_layout = Settings::LayoutOption::Default;
    else if (ui.action_Screen_Layout_Single_Screen->isChecked())
        new_layout = Settings::LayoutOption::SingleScreen;
    else if (ui.action_Screen_Layout_Medium_Screen->isChecked())
        new_layout = Settings::LayoutOption::MediumScreen;
    else if (ui.action_Screen_Layout_Large_Screen->isChecked())
        new_layout = Settings::LayoutOption::LargeScreen;
    else if (ui.action_Screen_Layout_Side_by_Side->isChecked())
        new_layout = Settings::LayoutOption::SideScreen;
    Settings::values.layout_option = new_layout;
    Settings::Apply();
}

void GMainWindow::ToggleScreenLayout() {
    Settings::LayoutOption new_layout{Settings::LayoutOption::Default};
    switch (Settings::values.layout_option) {
    case Settings::LayoutOption::Default:
        new_layout = Settings::LayoutOption::SingleScreen;
        break;
    case Settings::LayoutOption::SingleScreen:
        new_layout = Settings::LayoutOption::MediumScreen;
        break;
    case Settings::LayoutOption::MediumScreen:
        new_layout = Settings::LayoutOption::LargeScreen;
        break;
    case Settings::LayoutOption::LargeScreen:
        new_layout = Settings::LayoutOption::SideScreen;
        break;
    case Settings::LayoutOption::SideScreen:
        new_layout = Settings::LayoutOption::Default;
        break;
    }
    Settings::values.layout_option = new_layout;
    SyncMenuUISettings();
    Settings::Apply();
}

void GMainWindow::OnSwapScreens() {
    Settings::values.swap_screen = ui.action_Screen_Layout_Swap_Screens->isChecked();
    Settings::Apply();
}

void GMainWindow::ToggleSleepMode() {
    system.SetSleepModeEnabled(ui.action_Sleep_Mode->isChecked());
}

void GMainWindow::OnOpenConfiguration() {
    ConfigurationDialog configuration_dialog{this, hotkey_registry};
    auto old_theme{UISettings::values.theme};
    auto old_profile{Settings::values.profile};
    auto old_profiles{Settings::values.profiles};
    auto old_disable_mh_2xmsaa{Settings::values.disable_mh_2xmsaa};
    auto old_enable_discord_rpc{UISettings::values.enable_discord_rpc};
    auto result{configuration_dialog.exec()};
    if (result == QDialog::Accepted) {
        if (configuration_dialog.restore_defaults_requested) {
            config->RestoreDefaults();
            UpdateUITheme();
            emit UpdateThemedIcons();
            SyncMenuUISettings();
            app_list->Refresh();
        } else {
            configuration_dialog.ApplyConfiguration();
            if (UISettings::values.theme != old_theme) {
                UpdateUITheme();
                emit UpdateThemedIcons();
            }
            if (Settings::values.disable_mh_2xmsaa != old_disable_mh_2xmsaa) {
                if (system.IsPoweredOn())
                    system.Kernel().GetSharedPageHandler().Update3DSettings();
                if (control_panel)
                    control_panel->Update3D();
            }
            SyncMenuUISettings();
            app_list->Refresh();
            config->Save();
        }
#ifdef ENABLE_DISCORD_RPC
        if (old_enable_discord_rpc != UISettings::values.enable_discord_rpc)
            if (UISettings::values.enable_discord_rpc)
                InitializeDiscordRPC();
            else
                ShutdownDiscordRPC();
#endif
    } else {
        Settings::values.profiles = old_profiles;
        Settings::LoadProfile(old_profile);
    }
}

void GMainWindow::OnCheats() {
    if (!cheats_window)
        cheats_window = std::make_shared<CheatDialog>(system, this);
    cheats_window->show();
}

void GMainWindow::OnControlPanel() {
    if (!control_panel)
        control_panel = std::make_shared<ControlPanel>(system, this);
    control_panel->show();
}

void GMainWindow::OnSetPlayCoins() {
    bool ok{};
    u16 play_coins{static_cast<u16>(
        QInputDialog::getInt(this, "Set Play Coins", "Play Coins:", 0, 0, 300, 1, &ok,
                             Qt::WindowSystemMenuHint | Qt::WindowTitleHint))};
    if (ok)
        Service::PTM::SetPlayCoins(play_coins);
}

void GMainWindow::OnOpenUserDirectory() {
    QString path{QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::UserDir))};
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(path.replace("./user", QDir::currentPath() + "/user")));
}

void GMainWindow::OnNANDDefault() {
    Settings::values.nand_dir.clear();
    app_list->PopulateAsync(UISettings::values.app_dirs);
}

void GMainWindow::OnNANDCustom() {
    QString dir{QFileDialog::getExistingDirectory(this, "Set NAND Directory", ".")};
    if (dir.isEmpty())
        return;
    Settings::values.nand_dir = dir.toStdString();
    app_list->PopulateAsync(UISettings::values.app_dirs);
}

void GMainWindow::OnSDMCDefault() {
    Settings::values.sdmc_dir.clear();
    app_list->PopulateAsync(UISettings::values.app_dirs);
}

void GMainWindow::OnSDMCCustom() {
    QString dir{QFileDialog::getExistingDirectory(this, "Set SD Card Directory", ".")};
    if (dir.isEmpty())
        return;
    Settings::values.sdmc_dir = dir.toStdString();
    app_list->PopulateAsync(UISettings::values.app_dirs);
}

void GMainWindow::OnLoadAmiibo() {
    OnPauseApplication();
    const QString file_filter{QString("Amiibo File") + " (*.bin);;" + "All Files (*.*)"};
    const QString filename{QFileDialog::getOpenFileName(this, "Load Amiibo", ".", file_filter)};
    OnStartApplication();
    if (!filename.isEmpty()) {
        std::string filename_std{filename.toStdString()};
        FileUtil::IOFile file{filename_std, "rb"};
        if (!file) {
            QMessageBox::critical(
                this, "Error opening Amiibo data file",
                QString("Unable to open Amiibo file \"%1\" for reading.").arg(filename));
            return;
        }
        Service::NFC::AmiiboData data;
        std::size_t read{file.ReadBytes(data.data(), data.size())};
        if (read < 540) {
            QMessageBox::critical(
                this, "Error reading Amiibo data file",
                QString("Unable to fully read Amiibo data. minimum file size is 540 bytes, but "
                        "was only able to read %1 bytes.")
                    .arg(read));
            return;
        }
        auto nfc{system.ServiceManager().GetService<Service::NFC::Module::Interface>("nfc:u")};
        nfc->LoadAmiibo(std::move(data), std::move(filename_std));
        ui.action_Remove_Amiibo->setEnabled(true);
    }
}

void GMainWindow::OnRemoveAmiibo() {
    auto nfc{system.ServiceManager().GetService<Service::NFC::Module::Interface>("nfc:u")};
    nfc->RemoveAmiibo();
    ui.action_Remove_Amiibo->setEnabled(false);
}

void GMainWindow::OnToggleFilterBar() {
    app_list->setFilterVisible(ui.action_Show_Filter_Bar->isChecked());
    if (ui.action_Show_Filter_Bar->isChecked())
        app_list->setFilterFocus();
    else
        app_list->clearFilter();
}

void GMainWindow::OnRecordMovie() {
    if (system.IsPoweredOn()) {
        QMessageBox::StandardButton answer{QMessageBox::warning(
            this, "Record Movie",
            "To keep consistency with the RNG, it is recommended to record the movie from game "
            "start.<br>Are you sure you still want to record movies now?",
            QMessageBox::Yes | QMessageBox::No)};
        if (answer == QMessageBox::No)
            return;
    }
    const QString path{
        QFileDialog::getSaveFileName(this, "Record Movie", ".", "Citra TAS Movie (*.ctm)")};
    if (path.isEmpty())
        return;
    if (system.IsPoweredOn())
        Core::Movie::GetInstance().StartRecording(path.toStdString());
    else {
        movie_record_on_start = true;
        movie_record_path = path;
        QMessageBox::information(this, "Record Movie",
                                 "Recording will start once you boot a game.");
    }
    ui.action_Record_Movie->setEnabled(false);
    ui.action_Play_Movie->setEnabled(false);
    ui.action_Stop_Recording_Playback->setEnabled(true);
}

bool GMainWindow::ValidateMovie(const QString& path, u64 program_id) {
    using namespace Core;
    Movie::ValidationResult result{
        Core::Movie::GetInstance().ValidateMovie(path.toStdString(), program_id)};
    QMessageBox::StandardButton answer;
    switch (result) {
    case Movie::ValidationResult::RevisionDismatch:
        answer = QMessageBox::question(
            this, "Revision Dismatch",
            "The movie file you are trying to load was created on a different revision of Citra."
            "<br/>Citra has had some changes during the time, and the playback may desync or not "
            "work as expected."
            "<br/><br/>Are you sure you still want to load the movie file?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return false;
        break;
    case Movie::ValidationResult::AppDismatch:
        answer = QMessageBox::question(
            this, "Application Dismatch",
            "The movie file you are trying to load was recorded with a different application."
            "<br/>The playback may not work as expected, and it may cause unexpected results."
            "<br/><br/>Are you sure you still want to load the movie file?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return false;
        break;
    case Movie::ValidationResult::Invalid:
        QMessageBox::critical(
            this, "Invalid Movie File",
            "The movie file you are trying to load is invalid."
            "<br/>Either the file is corrupted, or Citra has had made some major changes to the "
            "Movie module."
            "<br/>Please choose a different movie file and try again.");
        return false;
    default:
        break;
    }
    return true;
}

void GMainWindow::OnPlayMovie() {
    if (system.IsPoweredOn()) {
        QMessageBox::StandardButton answer{QMessageBox::warning(
            this, "Play Movie",
            "To keep consistency with the RNG, it is recommended to play the movie from game "
            "start.<br>Are you sure you still want to play movies now?",
            QMessageBox::Yes | QMessageBox::No)};
        if (answer == QMessageBox::No)
            return;
    }
    const QString path{
        QFileDialog::getOpenFileName(this, "Play Movie", ".", "Citra TAS Movie (*.ctm)")};
    if (path.isEmpty())
        return;
    if (system.IsPoweredOn()) {
        if (!ValidateMovie(path))
            return;
    } else {
        u64 program_id{Core::Movie::GetInstance().GetMovieProgramID(path.toStdString())};
        QString app_path{app_list->FindApplicationByProgramID(program_id)};
        if (app_path.isEmpty()) {
            const int num_recent_files{
                std::min(UISettings::values.recent_files.size(), max_recent_files_item)};
            for (int i{}; i < num_recent_files; i++) {
                QString action_path{actions_recent_files[i]->data().toString()};
                if (!action_path.isEmpty()) {
                    if (QFile::exists(path)) {
                        auto loader{Loader::GetLoader(action_path.toStdString())};
                        u64 program_id_file;
                        if (loader->ReadProgramId(program_id_file) ==
                                Loader::ResultStatus::Success &&
                            program_id_file == program_id)
                            app_path = action_path;
                    }
                }
            }
            if (app_path.isEmpty()) {
                QMessageBox::warning(
                    this, "Application Not Found",
                    "The movie you are trying to play is from a application that isn't "
                    "in the application list and isn't in the recent files. If you have "
                    "the application, add the folder containing it to the application list or open "
                    "the "
                    "application and try to play the movie again.");
                return;
            }
        }
        if (!ValidateMovie(path, program_id))
            return;
        Core::Movie::GetInstance().PrepareForPlayback(path.toStdString());
        BootApplication(app_path.toStdString());
    }
    Core::Movie::GetInstance().StartPlayback(path.toStdString(), [this] {
        QMetaObject::invokeMethod(this, "OnMoviePlaybackCompleted");
    });
    ui.action_Record_Movie->setEnabled(false);
    ui.action_Play_Movie->setEnabled(false);
    ui.action_Stop_Recording_Playback->setEnabled(true);
}

void GMainWindow::OnStopRecordingPlayback() {
    if (movie_record_on_start) {
        QMessageBox::information(this, "Record Movie", "Movie recording cancelled.");
        movie_record_on_start = false;
        movie_record_path.clear();
    } else {
        const bool was_recording{Core::Movie::GetInstance().IsRecordingInput()};
        Core::Movie::GetInstance().Shutdown();
        if (was_recording)
            QMessageBox::information(this, "Movie Saved", "The movie is successfully saved.");
    }
    ui.action_Record_Movie->setEnabled(true);
    ui.action_Play_Movie->setEnabled(true);
    ui.action_Stop_Recording_Playback->setEnabled(false);
}

void GMainWindow::OnCaptureScreenshot() {
    OnPauseApplication();
    const QString path{
        QFileDialog::getSaveFileName(this, "Capture Screenshot", ".", "PNG Image (*.png)")};
    OnStartApplication();
    if (path.isEmpty())
        return;
    screens->CaptureScreenshot(UISettings::values.screenshot_resolution_factor, path);
}

void GMainWindow::UpdatePerformanceStats() {
    if (!emu_thread) {
        perf_stats_update_timer.stop();
        return;
    }
    auto results{system.GetAndResetPerfStats()};
    if (Settings::values.use_frame_limit)
        perf_stats_label->setText(QString("%1 % / %2 % | %3 FPS | %4 ms")
                                      .arg(results.emulation_speed * 100.0, 0, 'f', 0)
                                      .arg(Settings::values.frame_limit)
                                      .arg(results.app_fps, 0, 'f', 0)
                                      .arg(results.frametime * 1000.0, 0, 'f', 2));
    else
        perf_stats_label->setText(QString("%1 % | %2 FPS | %3 ms")
                                      .arg(results.emulation_speed * 100.0, 0, 'f', 0)
                                      .arg(results.app_fps, 0, 'f', 0)
                                      .arg(results.frametime * 1000.0, 0, 'f', 2));
    perf_stats_label->setVisible(true);
}

void GMainWindow::OnCoreError(Core::System::ResultStatus result, const std::string& details) {
    QString message, title, status_message;
    switch (result) {
    case Core::System::ResultStatus::ErrorSystemFiles: {
        const QString common_message{
            "%1 is missing. Please <a "
            "href='https://github.com/citra-valentin/citra/wiki/"
            "Dumping-System-Archives-from-a-Console/'>dump your system "
            "archives</a>.<br/>Continuing emulation may result in crashes and bugs."};
        if (!details.empty())
            message = common_message.arg(QString::fromStdString(details));
        else
            message = common_message.arg("A system archive");
        title = "System Archive Not Found";
        status_message = "System Archive Missing";
        break;
    }
    case Core::System::ResultStatus::ShutdownRequested:
        if (cheats_window)
            cheats_window->close();
        break;
    case Core::System::ResultStatus::FatalError:
        message = "A fatal error occured. Check the log for details.<br/>Continuing emulation may "
                  "result in crashes and bugs.";
        title = "Fatal Error";
        status_message = "Fatal Error encountered";
        break;
    default:
        UNREACHABLE_MSG("Unhandled result status {}", static_cast<u32>(result));
        break;
    }
    QMessageBox message_box;
    message_box.setWindowTitle(title);
    message_box.setText(message);
    message_box.setIcon(QMessageBox::Icon::Critical);
    QPushButton* continue_button{message_box.addButton("Continue", QMessageBox::RejectRole)};
    QPushButton* abort_button{message_box.addButton("Abort", QMessageBox::AcceptRole)};
    if (result != Core::System::ResultStatus::ShutdownRequested)
        message_box.exec();
    if (result == Core::System::ResultStatus::ShutdownRequested ||
        message_box.clickedButton() == abort_button) {
        if (emu_thread) {
            ShutdownApplication();
            if (!system.set_application_file_path.empty()) {
                BootApplication(system.set_application_file_path);
                system.set_application_file_path.clear();
            }
        }
    } else {
        // Only show the message if the game is still running.
        if (emu_thread) {
            system.SetRunning(true);
            message_label->setText(status_message);
            message_label->setVisible(true);
        }
    }
}

void GMainWindow::OnMenuAboutCitra() {
    AboutDialog about{this};
    about.exec();
}

bool GMainWindow::ConfirmClose() {
    if (!emu_thread)
        return true;
    QMessageBox::StandardButton answer{
        QMessageBox::question(this, "Citra", "Are you sure you want to close Citra?",
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)};
    return answer != QMessageBox::No;
}

void GMainWindow::closeEvent(QCloseEvent* event) {
    if (!ConfirmClose()) {
        event->ignore();
        return;
    }
    if (!ui.action_Fullscreen->isChecked()) {
        UISettings::values.geometry = saveGeometry();
        UISettings::values.screens_geometry = screens->saveGeometry();
    }
    UISettings::values.state = saveState();
    UISettings::values.fullscreen = ui.action_Fullscreen->isChecked();
    UISettings::values.show_filter_bar = ui.action_Show_Filter_Bar->isChecked();
    UISettings::values.show_status_bar = ui.action_Show_Status_Bar->isChecked();
    app_list->SaveInterfaceLayout();
    hotkey_registry.SaveHotkeys();
    // Shutdown session if the emu thread is active...
    if (emu_thread)
        ShutdownApplication();
    screens->close();
    multiplayer_state->Close();
    QWidget::closeEvent(event);
}

static bool IsSingleFileDropEvent(QDropEvent* event) {
    const QMimeData* mimeData{event->mimeData()};
    return mimeData->hasUrls() && mimeData->urls().length() == 1;
}

void GMainWindow::dropEvent(QDropEvent* event) {
    if (IsSingleFileDropEvent(event) && ConfirmChangeApplication()) {
        const QMimeData* mimeData{event->mimeData()};
        QString filename{mimeData->urls().at(0).toLocalFile()};
        BootApplication(filename.toStdString());
    }
}

void GMainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (IsSingleFileDropEvent(event))
        event->acceptProposedAction();
}

void GMainWindow::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

bool GMainWindow::ConfirmChangeApplication() {
    if (!emu_thread)
        return true;
    auto answer{QMessageBox::question(
        this, "Citra",
        "Are you sure you want to stop the emulation? Any unsaved progress will be lost.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No)};
    return answer != QMessageBox::No;
}

void GMainWindow::filterBarSetChecked(bool state) {
    ui.action_Show_Filter_Bar->setChecked(state);
    emit(OnToggleFilterBar());
}

void GMainWindow::UpdateUITheme() {
    QStringList theme_paths{default_theme_paths};
    if (UISettings::values.theme != UISettings::themes[0].second &&
        !UISettings::values.theme.isEmpty()) {
        const QString theme_uri{":" + UISettings::values.theme + "/style.qss"};
        QFile f{theme_uri};
        if (f.open(QFile::ReadOnly | QFile::Text)) {
            QTextStream ts{&f};
            qApp->setStyleSheet(ts.readAll());
            GMainWindow::setStyleSheet(ts.readAll());
        } else
            LOG_ERROR(Frontend, "Unable to set style, stylesheet file not found");
        theme_paths.append(QStringList{":/icons/default", ":/icons/" + UISettings::values.theme});
        QIcon::setThemeName(":/icons/" + UISettings::values.theme);
    } else {
        qApp->setStyleSheet("");
        GMainWindow::setStyleSheet("");
        theme_paths.append(QStringList{":/icons/default"});
        QIcon::setThemeName(":/icons/default");
    }
    QIcon::setThemeSearchPaths(theme_paths);
}

void GMainWindow::OnMoviePlaybackCompleted() {
    QMessageBox::information(this, "Playback Completed", "Movie playback completed.");
    ui.action_Record_Movie->setEnabled(true);
    ui.action_Play_Movie->setEnabled(true);
    ui.action_Stop_Recording_Playback->setEnabled(false);
}

void GMainWindow::UpdateTitle() {
    if (short_title.empty())
        setWindowTitle(
            QString("Citra | Valentin %1-%2").arg(Common::g_scm_branch, Common::g_scm_desc));
    else
        setWindowTitle(QString("Citra | Valentin %1-%2 | %3")
                           .arg(Common::g_scm_branch, Common::g_scm_desc, short_title.c_str()));
}

void GMainWindow::SyncMenuUISettings() {
    ui.action_Screen_Layout_Default->setChecked(Settings::values.layout_option ==
                                                Settings::LayoutOption::Default);
    ui.action_Screen_Layout_Single_Screen->setChecked(Settings::values.layout_option ==
                                                      Settings::LayoutOption::SingleScreen);
    ui.action_Screen_Layout_Medium_Screen->setChecked(Settings::values.layout_option ==
                                                      Settings::LayoutOption::MediumScreen);
    ui.action_Screen_Layout_Large_Screen->setChecked(Settings::values.layout_option ==
                                                     Settings::LayoutOption::LargeScreen);
    ui.action_Screen_Layout_Side_by_Side->setChecked(Settings::values.layout_option ==
                                                     Settings::LayoutOption::SideScreen);
    ui.action_Screen_Layout_Swap_Screens->setChecked(Settings::values.swap_screen);
}

void GMainWindow::InitializeDiscordRPC() {
#ifdef ENABLE_DISCORD_RPC
    DiscordEventHandlers handlers{};
    handlers.disconnected = HandleDiscordDisconnected;
    handlers.errored = HandleDiscordError;
    Discord_Initialize("472104565165260826", &handlers, 0, NULL);
    discord_rpc_start_time = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    auto& member{system.RoomMember()};
    callback_handle = member.BindOnRoomInformationChanged(
        [this](const Network::RoomInformation& info) { UpdateDiscordRPC(info); });
    UpdateDiscordRPC(member.GetRoomInformation());
#endif
}

void GMainWindow::ShutdownDiscordRPC() {
#ifdef ENABLE_DISCORD_RPC
    system.RoomMember().Unbind(callback_handle);
    Discord_ClearPresence();
    Discord_Shutdown();
#endif
}

void GMainWindow::UpdateDiscordRPC(const Network::RoomInformation& info) {
#ifdef ENABLE_DISCORD_RPC
    if (UISettings::values.enable_discord_rpc) {
        DiscordEventHandlers handlers{};
        handlers.disconnected = HandleDiscordDisconnected;
        handlers.errored = HandleDiscordError;
        DiscordRichPresence presence{};
        auto& member{system.RoomMember()};
        if (member.IsConnected()) {
            const auto& member_info{member.GetMemberInformation()};
            presence.partySize = member_info.size();
            presence.partyMax = info.member_slots;
            static std::string room_name;
            room_name = info.name;
            presence.state = room_name.c_str();
        }
        if (!short_title.empty())
            presence.details = short_title.c_str();
        presence.startTimestamp = discord_rpc_start_time;
        presence.largeImageKey = "icon";
        Discord_UpdatePresence(&presence);
    }
#endif
}

#ifdef main
#undef main
#endif

int main(int argc, char* argv[]) {
    Common::DetachedTasks detached_tasks;

    // Init settings params
    QCoreApplication::setOrganizationName("Citra team");
    QCoreApplication::setApplicationName("Citra");

    QApplication app{argc, argv};

    // Qt changes the locale and causes issues in float conversion using std::to_string() when
    // generating shaders
    setlocale(LC_ALL, "C");

    GMainWindow main_window;

    // Register camera factories
    Camera::RegisterFactory("image", std::make_unique<Camera::StillImageCameraFactory>());
    Camera::RegisterFactory("qt", std::make_unique<Camera::QtMultimediaCameraFactory>());
    Camera::QtMultimediaCameraHandler::Init();

    LOG_INFO(Frontend, "Citra version: Valentin {}-{}", Common::g_scm_branch, Common::g_scm_desc);
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
    main_window.show();
    int result{app.exec()};
#ifdef _WIN32
    WSACleanup();
#endif
    detached_tasks.WaitForAllTasks();
    return result;
}
