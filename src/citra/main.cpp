// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

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
#include "citra/bootmanager.h"
#include "citra/camera/qt_multimedia_camera.h"
#include "citra/camera/still_image_camera.h"
#include "citra/cheats.h"
#include "citra/configuration/config.h"
#include "citra/configuration/dialog.h"
#include "citra/control_panel.h"
#include "citra/hotkeys.h"
#include "citra/main.h"
#include "citra/multiplayer/state.h"
#include "citra/program_list.h"
#include "citra/ui_settings.h"
#include "citra/util/clickable_label.h"
#include "citra/util/util.h"
#include "common/common_paths.h"
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

GMainWindow::GMainWindow(Config& config, Core::System& system) : config{config}, system{system} {
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
    program_list->PopulateAsync(UISettings::values.program_dirs);
    auto args{QApplication::arguments()};
    if (args.length() >= 2)
        BootProgram(args[1].toStdString());
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
    screens = new Screens(this, emu_thread.get(), system);
    screens->hide();
    program_list = new ProgramList(system, this);
    ui.horizontalLayout->addWidget(program_list);
    program_list_placeholder = new ProgramListPlaceholder(this);
    ui.horizontalLayout->addWidget(program_list_placeholder);
    program_list_placeholder->setVisible(false);
    multiplayer_state =
        new MultiplayerState(this, ui.action_Leave_Room, ui.action_Show_Room, system);
    multiplayer_state->setVisible(false);
    // Create status bar
    message_label = new QLabel();
    // Configured separately for left alignment
    message_label->hide();
    message_label->setFrameStyle(QFrame::NoFrame);
    message_label->setContentsMargins(4, 0, 4, 0);
    message_label->setAlignment(Qt::AlignLeft);
    statusBar()->addPermanentWidget(message_label, 1);
    progress_bar = new QProgressBar();
    progress_bar->setMaximum(INT_MAX);
    progress_bar->hide();
    statusBar()->addPermanentWidget(progress_bar);
    emu_speed_label = new QLabel();
    emu_speed_label->setToolTip("Current emulation speed. Values higher or lower than 100% "
                                "indicate emulation is running faster or slower than a console.");
    fps_label = new QLabel();
    fps_label->setToolTip("How many frames per second the program is currently displaying.");
    emu_frametime_label = new QLabel();
    emu_frametime_label->setToolTip(
        "Time taken to emulate a frame, not counting framelimiting or v-sync. For "
        "full-speed emulation this should be at most 16.67 ms (with screen refresh rate set to "
        "60).");
    emu_frametime_label = new QLabel();
    emu_frametime_label->setToolTip(
        "Time taken to emulate a frame, not counting framelimiting or v-sync. For "
        "full-speed emulation this should be at most 16.67 ms (with screen refresh rate set to "
        "60).");
    touch_screen_pos_label = new QLabel();
    for (auto& label : {touch_screen_pos_label, emu_speed_label, fps_label, emu_frametime_label}) {
        label->hide();
        label->setFrameStyle(QFrame::NoFrame);
        label->setContentsMargins(4, 0, 4, 0);
        statusBar()->addPermanentWidget(label, 0);
    }
    statusBar()->addPermanentWidget(multiplayer_state->GetStatusIcon(), 0);
    statusBar()->setVisible(true);
    // Removes an ugly inner border from the status bar widgets under Linux
    setStyleSheet("QStatusBar::item{border: none;}");
    auto actionGroup_ScreenLayouts{new QActionGroup(this)};
    actionGroup_ScreenLayouts->addAction(ui.action_Screen_Layout_Default);
    actionGroup_ScreenLayouts->addAction(ui.action_Screen_Layout_Single_Screen);
    actionGroup_ScreenLayouts->addAction(ui.action_Screen_Layout_Medium_Screen);
    actionGroup_ScreenLayouts->addAction(ui.action_Screen_Layout_Large_Screen);
    actionGroup_ScreenLayouts->addAction(ui.action_Screen_Layout_Side_by_Side);
    auto actionGroup_NAND{new QActionGroup(this)};
    actionGroup_NAND->addAction(ui.action_NAND_Default);
    actionGroup_NAND->addAction(ui.action_NAND_Custom);
    auto actionGroup_SDMC{new QActionGroup(this)};
    actionGroup_SDMC->addAction(ui.action_SDMC_Default);
    actionGroup_SDMC->addAction(ui.action_SDMC_Custom);
}

void GMainWindow::InitializeRecentFileMenuActions() {
    for (int i{}; i < MaxRecentFiles; ++i) {
        actions_recent_files[i] = new QAction(this);
        actions_recent_files[i]->setVisible(false);
        connect(actions_recent_files[i], &QAction::triggered, this, &GMainWindow::OnMenuRecentFile);
        ui.menu_recent_files->addAction(actions_recent_files[i]);
    }
    ui.menu_recent_files->addSeparator();
    auto action_clear_recent_files{new QAction(this)};
    action_clear_recent_files->setText("Clear Recent Files");
    connect(action_clear_recent_files, &QAction::triggered, this, [this] {
        UISettings::values.recent_files.clear();
        UpdateRecentFiles();
    });
    ui.menu_recent_files->addAction(action_clear_recent_files);
    UpdateRecentFiles();
}

void GMainWindow::InitializeHotkeys() {
    hotkey_registry.LoadHotkeys();
    ui.action_Load_File->setShortcut(hotkey_registry.GetKeySequence("Main Window", "Load File"));
    ui.action_Load_File->setShortcutContext(
        hotkey_registry.GetShortcutContext("Main Window", "Load File"));
    ui.action_Exit->setShortcut(hotkey_registry.GetKeySequence("Main Window", "Exit Citra"));
    ui.action_Exit->setShortcutContext(
        hotkey_registry.GetShortcutContext("Main Window", "Exit Citra"));
    ui.action_Stop->setShortcut(hotkey_registry.GetKeySequence("Main Window", "Stop Emulation"));
    ui.action_Stop->setShortcutContext(
        hotkey_registry.GetShortcutContext("Main Window", "Stop Emulation"));
    ui.action_Restart->setShortcut(
        hotkey_registry.GetKeySequence("Main Window", "Restart Emulation"));
    ui.action_Restart->setShortcutContext(
        hotkey_registry.GetShortcutContext("Main Window", "Restart Emulation"));
    ui.action_Show_Filter_Bar->setShortcut(
        hotkey_registry.GetKeySequence("Main Window", "Toggle Filter Bar"));
    ui.action_Show_Filter_Bar->setShortcutContext(
        hotkey_registry.GetShortcutContext("Main Window", "Toggle Filter Bar"));
    ui.action_Show_Status_Bar->setShortcut(
        hotkey_registry.GetKeySequence("Main Window", "Toggle Status Bar"));
    ui.action_Show_Status_Bar->setShortcutContext(
        hotkey_registry.GetShortcutContext("Main Window", "Toggle Status Bar"));
    ui.action_Sleep_Mode->setShortcut(
        hotkey_registry.GetKeySequence("Main Window", "Toggle Sleep Mode"));
    ui.action_Sleep_Mode->setShortcutContext(
        hotkey_registry.GetShortcutContext("Main Window", "Toggle Sleep Mode"));
    connect(hotkey_registry.GetHotkey("Main Window", "Load File", this), &QShortcut::activated,
            this, &GMainWindow::OnMenuLoadFile);
    connect(hotkey_registry.GetHotkey("Main Window", "Continue/Pause Emulation", this),
            &QShortcut::activated, this, [&] {
                if (system.IsPoweredOn())
                    if (system.IsRunning())
                        OnPauseProgram();
                    else
                        OnStartProgram();
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
                if (system.IsRunning()) {
                    ui.action_Fullscreen->setChecked(false);
                    ToggleFullscreen();
                }
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Toggle Speed Limit", this),
            &QShortcut::activated, this, [&] {
                Settings::values.use_frame_limit = !Settings::values.use_frame_limit;
                UpdateStatusBar();
            });
    constexpr u16 SPEED_LIMIT_STEP{5};
    connect(hotkey_registry.GetHotkey("Main Window", "Increase Speed Limit", this),
            &QShortcut::activated, this, [&] {
                if (Settings::values.frame_limit < 9999 - SPEED_LIMIT_STEP) {
                    Settings::values.frame_limit += SPEED_LIMIT_STEP;
                    UpdateStatusBar();
                }
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Decrease Speed Limit", this),
            &QShortcut::activated, this, [&] {
                if (Settings::values.frame_limit > SPEED_LIMIT_STEP) {
                    Settings::values.frame_limit -= SPEED_LIMIT_STEP;
                    UpdateStatusBar();
                }
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Toggle Frame Advancing", this),
            &QShortcut::activated, ui.action_Enable_Frame_Advancing, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Advance Frame", this), &QShortcut::activated,
            ui.action_Advance_Frame, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Load Amiibo", this), &QShortcut::activated,
            this, [&] {
                if (ui.action_Load_Amiibo->isEnabled())
                    OnLoadAmiibo();
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Remove Amiibo", this), &QShortcut::activated,
            this, [&] {
                if (ui.action_Remove_Amiibo->isEnabled())
                    OnRemoveAmiibo();
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Capture Screenshot", this),
            &QShortcut::activated, this, [&] {
                if (system.IsPoweredOn())
                    OnCaptureScreenshot();
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Change CPU Ticks", this),
            &QShortcut::activated, this, [&] {
                auto str{QInputDialog::getText(this, "Change CPU Ticks", "Ticks:")};
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
}

void GMainWindow::SetDefaultUIGeometry() {
    // Geometry: 55% of the window contents are in the upper screen half, 45% in the lower half
    const auto screenRect{QApplication::desktop()->screenGeometry(this)};
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
    program_list->LoadInterfaceLayout();
    screens->BackupGeometry();
    ui.horizontalLayout->addWidget(screens);
    screens->setFocusPolicy(Qt::ClickFocus);
    ui.action_Fullscreen->setChecked(UISettings::values.fullscreen);
    SyncMenuUISettings();
    ui.action_Show_Filter_Bar->setChecked(UISettings::values.show_filter_bar);
    program_list->setFilterVisible(ui.action_Show_Filter_Bar->isChecked());
    ui.action_Show_Status_Bar->setChecked(UISettings::values.show_status_bar);
    statusBar()->setVisible(ui.action_Show_Status_Bar->isChecked());
    ui.action_NAND_Default->setChecked(Settings::values.nand_dir.empty());
    ui.action_NAND_Custom->setChecked(!Settings::values.nand_dir.empty());
    ui.action_SDMC_Default->setChecked(Settings::values.sdmc_dir.empty());
    ui.action_SDMC_Custom->setChecked(!Settings::values.sdmc_dir.empty());
}

void GMainWindow::ConnectWidgetEvents() {
    connect(program_list, &ProgramList::ProgramChosen, this, &GMainWindow::OnProgramListLoadFile);
    connect(program_list, &ProgramList::OpenDirectory, this,
            &GMainWindow::OnProgramListOpenDirectory);
    connect(program_list, &ProgramList::OpenFolderRequested, this,
            &GMainWindow::OnProgramListOpenFolder);
    connect(program_list, &ProgramList::AddDirectory, this,
            &GMainWindow::OnProgramListAddDirectory);
    connect(program_list_placeholder, &ProgramListPlaceholder::AddDirectory, this,
            &GMainWindow::OnProgramListAddDirectory);
    connect(program_list, &ProgramList::ShowList, this, &GMainWindow::OnProgramListShowList);
    connect(this, &GMainWindow::EmulationStarting, screens, &Screens::OnEmulationStarting);
    connect(this, &GMainWindow::EmulationStopping, screens, &Screens::OnEmulationStopping);
    connect(&status_bar_update_timer, &QTimer::timeout, this, &GMainWindow::UpdateStatusBar);
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
    connect(ui.action_Start, &QAction::triggered, this, &GMainWindow::OnStartProgram);
    connect(ui.action_Pause, &QAction::triggered, this, &GMainWindow::OnPauseProgram);
    connect(ui.action_Stop, &QAction::triggered, this, &GMainWindow::OnStopProgram);
    connect(ui.action_Restart, &QAction::triggered, this,
            [this] { BootProgram(system.GetFilePath()); });
    connect(ui.action_Sleep_Mode, &QAction::triggered, this, &GMainWindow::ToggleSleepMode);
    connect(ui.action_Configuration, &QAction::triggered, this, &GMainWindow::OnOpenConfiguration);
    connect(ui.action_Cheats, &QAction::triggered, this, &GMainWindow::OnCheats);
    connect(ui.action_Control_Panel, &QAction::triggered, this, &GMainWindow::OnControlPanel);
    connect(ui.action_Dump_RAM, &QAction::triggered, this, &GMainWindow::OnDumpRAM);

    // View
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
    connect(ui.action_Screen_Layout_Custom_Layout, &QAction::triggered, this,
            &GMainWindow::OnCustomLayout);

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
            &MultiplayerState::OnCloseRoomClient);
    connect(ui.action_Connect_To_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnDirectConnect);
    connect(ui.action_Show_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnOpenRoom);

    // Help
    connect(ui.action_About, &QAction::triggered, this, &GMainWindow::OnMenuAboutCitra);
}

bool GMainWindow::LoadProgram(const std::string& filename) {
    // Shutdown previous session if the emu thread is still active...
    if (emu_thread)
        ShutdownProgram();
    screens->InitRenderTarget();
    screens->MakeCurrent();
    if (!gladLoadGL()) {
        QMessageBox::critical(this, "OpenGL 3.3 Unsupported",
                              "Your GPU may not support OpenGL 3.3, or you don't "
                              "have the latest graphics driver.");
        return false;
    }
    const auto result{system.Load(*screens, filename)};
    if (result != Core::System::ResultStatus::Success) {
        switch (result) {
        case Core::System::ResultStatus::ErrorGetLoader:
            LOG_ERROR(Frontend, "Failed to obtain loader for {}!", filename);
            QMessageBox::critical(
                this, "Unsupported Format",
                "Your file's format isn't supported.<br/>Please follow the guides to redump your "
                "<a "
                "href='https://github.com/valentinvanelslande/citra/wiki/"
                "Dumping-Game-Cartridges/'>game "
                "cartridges</a> or "
                "<a "
                "href='https://github.com/valentinvanelslande/citra/wiki/"
                "Dumping-Programs-Titles/'>installed "
                "programs</a>.");
            break;
        case Core::System::ResultStatus::ErrorSystemMode:
            LOG_ERROR(Frontend, "Failed to load file!");
            QMessageBox::critical(
                this, "File Corrupted",
                "Your file is corrupted. <br/>Please follow the guides to redump your "
                "<a "
                "href='https://github.com/valentinvanelslande/citra/wiki/"
                "Dumping-Game-Cartridges/'>game "
                "cartridges</a> or "
                "<a "
                "href='https://github.com/valentinvanelslande/citra/wiki/"
                "Dumping-Installed-Programs/'>installed "
                "programs</a>.");
            break;
        case Core::System::ResultStatus::ErrorLoader_ErrorEncrypted:
            QMessageBox::critical(
                this, "File Encrypted",
                "Your file is encrypted. <br/>Please follow the guides to redump your "
                "<a "
                "href='https://github.com/valentinvanelslande/citra/wiki/"
                "Dumping-Game-Cartridges/'>game "
                "cartridges</a> or "
                "<a "
                "href='https://github.com/valentinvanelslande/citra/wiki/"
                "Dumping-Installed-Programs/'>installed "
                "programs</a>.");
            break;
        case Core::System::ResultStatus::ErrorLoader_ErrorInvalidFormat:
            QMessageBox::critical(
                this, "Unsupported Format",
                "Your file's format isn't supported.<br/>Please follow the guides to redump your "
                "<a "
                "href='https://github.com/valentinvanelslande/citra/wiki/"
                "Dumping-Game-Cartridges/'>game "
                "cartridges</a> or "
                "<a "
                "href='https://github.com/valentinvanelslande/citra/wiki/"
                "Dumping-Installed-Programs/'>installed "
                "programs</a>.");
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
                "You're running default Windows drivers "
                "for your GPU. You need to install the "
                "proper drivers for your graphics card from the manufacturer's website.");
            break;
        case Core::System::ResultStatus::ErrorVideoCore_ErrorBelowGL33:
            QMessageBox::critical(this, "OpenGL 3.3 Unsupported",
                                  "Your GPU may not support OpenGL 3.3, or you don't "
                                  "have the latest graphics driver.");
            break;
        default:
            QMessageBox::critical(this, "Error while loading file!",
                                  "An unknown error occured. Please see the log for more details.");
            break;
        }
        return false;
    }
    system.GetProgramLoader().ReadShortTitle(short_title);
    UpdateTitle();
    return true;
}

void GMainWindow::BootProgram(const std::string& filename) {
    LOG_INFO(Frontend, "Booting {}", filename);
    StoreRecentFile(QString::fromStdString(filename)); // Put the filename on top of the list
    auto& movie{system.MovieSystem()};
    if (movie_record_on_start)
        movie.PrepareForRecording();
    if (!LoadProgram(filename))
        return;
    // Create and start the emulation thread
    emu_thread = std::make_unique<EmuThread>(system, screens);
    emit EmulationStarting(emu_thread.get());
    screens->moveContext();
    emu_thread->start();
    connect(screens, &Screens::Closed, this, &GMainWindow::OnStopProgram);
    connect(screens, &Screens::TouchScreenPositionChanged, this,
            &GMainWindow::OnTouchScreenPositionChanged);
    // Update the GUI
    program_list->hide();
    program_list_placeholder->hide();
    status_bar_update_timer.start(2000);
    screens->show();
    screens->setFocus();
    if (ui.action_Fullscreen->isChecked())
        ShowFullscreen();
    OnStartProgram();
    if (movie_record_on_start) {
        movie.StartRecording(movie_record_path.toStdString());
        movie_record_on_start = false;
        movie_record_path.clear();
    }
    connect(emu_thread.get(), &EmuThread::ErrorThrown, this, &GMainWindow::OnCoreError);
    // Update Discord RPC
    auto& member{system.RoomMember()};
    if (!member.IsConnected())
        UpdateDiscordRPC(member.GetRoomInformation());
}

void GMainWindow::ShutdownProgram() {
    OnStopRecordingPlayback();
    emu_thread->RequestStop();
    // Frame advancing must be cancelled in order to release the emu thread from waiting
    system.frame_limiter.SetFrameAdvancing(false);

    emit EmulationStopping();
    // Wait for emulation thread to complete and delete it
    emu_thread->wait();
    emu_thread = nullptr;

    Camera::QtMultimediaCameraHandler::ReleaseHandlers();
    // The emulation is stopped, so closing the window or not doesn't matter anymore
    disconnect(screens, &Screens::Closed, this, &GMainWindow::OnStopProgram);
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
    ui.action_Dump_RAM->setEnabled(false);
    screens->hide();
    if (program_list->isEmpty())
        program_list_placeholder->show();
    else
        program_list->show();
    program_list->setFilterFocus();
    // Disable status bar updates
    status_bar_update_timer.stop();
    message_label->setVisible(false);
    emu_speed_label->setVisible(false);
    fps_label->setVisible(false);
    emu_frametime_label->setVisible(false);
    touch_screen_pos_label->setVisible(false);

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
    while (UISettings::values.recent_files.size() > MaxRecentFiles)
        UISettings::values.recent_files.removeLast();
    UpdateRecentFiles();
}

void GMainWindow::Update3D() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "Update3D", Qt::BlockingQueuedConnection);
        return;
    }
    if (control_panel)
        control_panel->Update3D();
}

void GMainWindow::UpdateNetwork() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "UpdateNetwork", Qt::BlockingQueuedConnection);
        return;
    }
    if (control_panel)
        control_panel->UpdateNetwork();
}

void GMainWindow::UpdateFrameAdvancing() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "UpdateFrameAdvancing", Qt::BlockingQueuedConnection);
        return;
    }
    const bool enabled{system.frame_limiter.GetFrameAdvancing()};
    ui.action_Enable_Frame_Advancing->setChecked(enabled);
    ui.action_Advance_Frame->setEnabled(enabled);
}

void GMainWindow::UpdateRecentFiles() {
    const int num_recent_files{std::min(UISettings::values.recent_files.size(), MaxRecentFiles)};
    for (int i{}; i < num_recent_files; i++) {
        const auto text{QString("%1. %2").arg(i + 1).arg(
            QFileInfo(UISettings::values.recent_files[i]).fileName())};
        actions_recent_files[i]->setText(text);
        actions_recent_files[i]->setData(UISettings::values.recent_files[i]);
        actions_recent_files[i]->setToolTip(UISettings::values.recent_files[i]);
        actions_recent_files[i]->setVisible(true);
    }
    for (int j{num_recent_files}; j < MaxRecentFiles; ++j)
        actions_recent_files[j]->setVisible(false);
    // Enable the recent files menu if the list isn't empty
    ui.menu_recent_files->setEnabled(num_recent_files != 0);
}

void GMainWindow::OnProgramListLoadFile(const QString& path) {
    BootProgram(path.toStdString());
}

void GMainWindow::OnProgramListOpenFolder(u64 data_id, ProgramListOpenTarget target) {
    std::string path, open_target;
    switch (target) {
    case ProgramListOpenTarget::SaveData:
        open_target = "Save Data";
        path = FileSys::ArchiveSource_SDSaveData::GetSaveDataPathFor(
            FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir, Settings::values.sdmc_dir), data_id);
        break;
    case ProgramListOpenTarget::ExtData:
        open_target = "Extra Data";
        path = FileSys::GetExtDataPathFromID(
            FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir, Settings::values.sdmc_dir), data_id);
        break;
    case ProgramListOpenTarget::Program:
        open_target = "Program";
        path = Service::AM::GetProgramPath(Service::AM::GetProgramMediaType(data_id), data_id) +
               "content/";
        break;
    case ProgramListOpenTarget::UpdateData:
        open_target = "Update Data";
        path = Service::AM::GetProgramPath(Service::FS::MediaType::SDMC, data_id + 0xe00000000) +
               "content/";
        break;
    default:
        LOG_ERROR(Frontend, "Unexpected target {}", static_cast<int>(target));
        return;
    }
    auto qpath{QString::fromStdString(path)};
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

void GMainWindow::OnProgramListOpenDirectory(const QString& directory) {
    QString path;
    if (directory == "INSTALLED")
        path = QString::fromStdString(
            FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir, Settings::values.sdmc_dir) +
            "Nintendo "
            "3DS/00000000000000000000000000000000/00000000000000000000000000000000/title/00040000");
    else if (directory == "SYSTEM")
        path = QString::fromStdString(
            FileUtil::GetUserPath(FileUtil::UserPath::NANDDir, Settings::values.nand_dir).c_str() +
            std::string("00000000000000000000000000000000/title/00040010"));
    else
        path = directory;
    if (!QFileInfo::exists(path)) {
        QMessageBox::critical(this, QString("Error Opening %1").arg(path), "Folder doesn't exist!");
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void GMainWindow::OnProgramListAddDirectory() {
    auto dir_path{QFileDialog::getExistingDirectory(this, "Select Directory")};
    if (dir_path.isEmpty())
        return;
    UISettings::AppDir program_dir{dir_path, false, true};
    if (!UISettings::values.program_dirs.contains(program_dir)) {
        UISettings::values.program_dirs.append(program_dir);
        program_list->PopulateAsync(UISettings::values.program_dirs);
    } else
        LOG_WARNING(Frontend, "Selected directory is already in the program list");
}

void GMainWindow::OnProgramListShowList(bool show) {
    if (system.IsPoweredOn())
        return;
    program_list->setVisible(show);
    program_list_placeholder->setVisible(!show);
}

void GMainWindow::OnMenuLoadFile() {
    const auto extensions{QString("*.").append(ProgramList::supported_file_extensions.join(" *."))};
    const auto file_filter{QString("3DS Executable (%1);;All Files (*.*)").arg(extensions)};
    const auto filename{QFileDialog::getOpenFileName(this, "Load File",
                                                     UISettings::values.programs_dir, file_filter)};
    if (filename.isEmpty())
        return;
    UISettings::values.programs_dir = QFileInfo(filename).path();
    BootProgram(filename.toStdString());
}

void GMainWindow::OnMenuInstallCIA() {
    auto filepaths{QFileDialog::getOpenFileNames(
        this, "Install CIA", ".", "CTR Importable Archive (*.cia);;All Files (*.*)")};
    if (filepaths.isEmpty())
        return;
    ui.action_Install_CIA->setEnabled(false);
    program_list->setDirectoryWatcherEnabled(false);
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
    const auto filepath{
        QFileDialog::getOpenFileName(this, "Add Seed", UISettings::values.seeds_dir)};
    if (filepath.isEmpty())
        return;
    UISettings::values.seeds_dir = QFileInfo(filepath).path();
    const auto program_id_s{QInputDialog::getText(this, "Citra", "Enter the program ID")};
    if (program_id_s.isEmpty())
        return;
    bool ok{};
    u64 program_id{program_id_s.toULongLong(&ok, 16)};
    if (!ok) {
        QMessageBox::critical(this, "Citra", "Invalid program ID");
        return;
    }
    FileSys::Seed seed{};
    seed.program_id = program_id;
    FileUtil::IOFile file{filepath.toStdString(), "rb"};
    if (file.ReadBytes(seed.data.data(), seed.data.size()) != seed.data.size()) {
        QMessageBox::critical(this, "Citra", "Failed to read seed data fully");
        return;
    }
    FileSys::SeedDB db;
    db.Load();
    db.Add(seed);
    db.Save();
    program_list->PopulateAsync(UISettings::values.program_dirs);
}

void GMainWindow::OnUpdateProgress(std::size_t written, std::size_t total) {
    progress_bar->setValue(
        static_cast<int>(INT_MAX * (static_cast<double>(written) / static_cast<double>(total))));
}

void GMainWindow::OnCIAInstallReport(Service::AM::InstallStatus status, const QString& filepath) {
    const auto filename{QFileInfo{filepath}.fileName()};
    switch (status) {
    case Service::AM::InstallStatus::Success:
        statusBar()->showMessage(QString("%1 installed").arg(filename));
        break;
    case Service::AM::InstallStatus::ErrorFailedToOpenFile:
        QMessageBox::critical(this, "Unable to open file",
                              QString("Couldn't open %1").arg(filename));
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
    program_list->setDirectoryWatcherEnabled(true);
    ui.action_Install_CIA->setEnabled(true);
    program_list->PopulateAsync(UISettings::values.program_dirs);
    if (system.IsPoweredOn()) {
        auto am{system.ServiceManager().GetService<Service::AM::AM_U>("am:u")->GetModule()};
        am->ScanForAllPrograms();
    }
}

void GMainWindow::OnMenuRecentFile() {
    auto action{qobject_cast<QAction*>(sender())};
    ASSERT(action);
    const auto filename{action->data().toString()};
    if (QFileInfo::exists(filename))
        BootProgram(filename.toStdString());
    else {
        // Display an error message and remove the file from the list.
        QMessageBox::information(this, "File not found",
                                 QString("File \"%1\" not found").arg(filename));
        UISettings::values.recent_files.removeOne(filename);
        UpdateRecentFiles();
    }
}

void GMainWindow::OnStartProgram() {
    Camera::QtMultimediaCameraHandler::ResumeCameras();
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
    ui.action_Sleep_Mode->setEnabled(true);
    ui.action_Sleep_Mode->setChecked(false);
    ui.action_Dump_RAM->setEnabled(true);
}

void GMainWindow::OnPauseProgram() {
    system.SetRunning(false);
    Camera::QtMultimediaCameraHandler::StopCameras();
    ui.action_Start->setEnabled(true);
    ui.action_Pause->setEnabled(false);
    ui.action_Stop->setEnabled(true);
    ui.action_Sleep_Mode->setEnabled(false);
}

void GMainWindow::OnStopProgram() {
    ShutdownProgram();
}

void GMainWindow::OnTouchScreenPositionChanged(unsigned x, unsigned y) {
    touch_screen_pos_label->setText(
        QString("Touch screen position: %1, %2").arg(QString::number(x), QString::number(y)));
    touch_screen_pos_label->show();
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
    auto new_layout{Settings::LayoutOption::Default};
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
    Settings::Apply(system);
}

void GMainWindow::ToggleScreenLayout() {
    auto new_layout{Settings::LayoutOption::Default};
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
    Settings::Apply(system);
}

void GMainWindow::OnSwapScreens() {
    Settings::values.swap_screens = ui.action_Screen_Layout_Swap_Screens->isChecked();
    Settings::Apply(system);
}

void GMainWindow::OnCustomLayout() {
    Settings::values.custom_layout = ui.action_Screen_Layout_Custom_Layout->isChecked();
    SyncMenuUISettings();
    Settings::Apply(system);
}

void GMainWindow::ToggleSleepMode() {
    if (!system.IsRunning()) {
        ui.action_Sleep_Mode->setChecked(false);
        return;
    }
    if (system.IsSleepModeEnabled()) {
        Camera::QtMultimediaCameraHandler::ResumeCameras();
        ui.action_Start->setEnabled(false);
        ui.action_Pause->setEnabled(true);
    } else {
        Camera::QtMultimediaCameraHandler::StopCameras();
        ui.action_Start->setEnabled(false);
        ui.action_Pause->setEnabled(false);
    }
    system.SetSleepModeEnabled(ui.action_Sleep_Mode->isChecked());
}

void GMainWindow::OnOpenConfiguration() {
    ConfigurationDialog configuration_dialog{this, hotkey_registry, system};
    configuration_dialog.restoreGeometry(UISettings::values.configuration_geometry);
    auto old_theme{UISettings::values.theme};
    auto old_profile{Settings::values.profile};
    auto old_profiles{Settings::values.profiles};
    auto old_disable_mh_2xmsaa{Settings::values.disable_mh_2xmsaa};
    auto old_enable_discord_rpc{UISettings::values.enable_discord_rpc};
    auto result{configuration_dialog.exec()};
    if (result == QDialog::Accepted) {
        if (configuration_dialog.restore_defaults_requested) {
            Settings::values.disable_mh_2xmsaa = old_disable_mh_2xmsaa;
            config.RestoreDefaults();
            UpdateUITheme();
            emit UpdateThemedIcons();
            SyncMenuUISettings();
            program_list->Refresh();
        } else {
            configuration_dialog.ApplyConfiguration();
            if (UISettings::values.theme != old_theme) {
                UpdateUITheme();
                emit UpdateThemedIcons();
            }
            if (Settings::values.disable_mh_2xmsaa != old_disable_mh_2xmsaa && control_panel)
                control_panel->Update3D();
            SyncMenuUISettings();
            program_list->Refresh();
            config.Save();
            UISettings::values.configuration_geometry = configuration_dialog.saveGeometry();
        }
#ifdef ENABLE_DISCORD_RPC
        if (old_enable_discord_rpc != UISettings::values.enable_discord_rpc)
            if (UISettings::values.enable_discord_rpc)
                InitializeDiscordRPC();
            else
                ShutdownDiscordRPC();
#endif
        InitializeHotkeys();
    } else {
        Settings::values.profiles = old_profiles;
        Settings::LoadProfile(old_profile);
    }
}

void GMainWindow::OnCheats() {
    CheatDialog dialog{system, this};
    dialog.exec();
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
    auto path{QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::UserDir))};
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(path.replace("./user", QDir::currentPath() + "/user")));
}

void GMainWindow::OnNANDDefault() {
    Settings::values.nand_dir.clear();
    program_list->PopulateAsync(UISettings::values.program_dirs);
}

void GMainWindow::OnNANDCustom() {
    auto dir{QFileDialog::getExistingDirectory(this, "Set NAND Directory", ".")};
    if (dir.isEmpty()) {
        ui.action_NAND_Default->setChecked(true);
        OnNANDDefault();
        return;
    }
    Settings::values.nand_dir = dir.toStdString();
    program_list->PopulateAsync(UISettings::values.program_dirs);
}

void GMainWindow::OnSDMCDefault() {
    Settings::values.sdmc_dir.clear();
    program_list->PopulateAsync(UISettings::values.program_dirs);
}

void GMainWindow::OnSDMCCustom() {
    auto dir{QFileDialog::getExistingDirectory(this, "Set SD Card Directory", ".")};
    if (dir.isEmpty()) {
        ui.action_SDMC_Default->setChecked(true);
        OnSDMCDefault();
        return;
    }
    Settings::values.sdmc_dir = dir.toStdString();
    program_list->PopulateAsync(UISettings::values.program_dirs);
}

void GMainWindow::OnLoadAmiibo() {
    if (system.IsSleepModeEnabled())
        return;
    OnPauseProgram();
    const auto file_filter{QString("Amiibo File") + " (*.bin);;" + "All Files (*.*)"};
    const auto filename{QFileDialog::getOpenFileName(this, "Load Amiibo",
                                                     UISettings::values.amiibo_dir, file_filter)};
    OnStartProgram();
    if (!filename.isEmpty()) {
        UISettings::values.amiibo_dir = QFileInfo(filename).path();
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
    if (system.IsSleepModeEnabled())
        return;
    auto nfc{system.ServiceManager().GetService<Service::NFC::Module::Interface>("nfc:u")};
    nfc->RemoveAmiibo();
    ui.action_Remove_Amiibo->setEnabled(false);
}

void GMainWindow::OnToggleFilterBar() {
    program_list->setFilterVisible(ui.action_Show_Filter_Bar->isChecked());
    if (ui.action_Show_Filter_Bar->isChecked())
        program_list->setFilterFocus();
    else
        program_list->clearFilter();
}

void GMainWindow::OnRecordMovie() {
    if (system.IsPoweredOn()) {
        auto answer{QMessageBox::warning(
            this, "Record Movie",
            "To keep consistency with the RNG, it is recommended to record the movie from program "
            "start.<br>Are you sure you still want to record movies now?",
            QMessageBox::Yes | QMessageBox::No)};
        if (answer == QMessageBox::No)
            return;
    }
    const auto path{QFileDialog::getSaveFileName(
        this, "Record Movie", UISettings::values.movies_dir, "Citra TAS Movie (*.ctm)")};
    if (path.isEmpty())
        return;
    UISettings::values.movies_dir = QFileInfo(path).path();
    if (system.IsPoweredOn())
        system.MovieSystem().StartRecording(path.toStdString());
    else {
        movie_record_on_start = true;
        movie_record_path = path;
        QMessageBox::information(this, "Record Movie",
                                 "Recording will start once you boot a program.");
    }
    ui.action_Record_Movie->setEnabled(false);
    ui.action_Play_Movie->setEnabled(false);
    ui.action_Stop_Recording_Playback->setEnabled(true);
}

bool GMainWindow::ValidateMovie(const QString& path, u64 program_id) {
    using namespace Core;
    auto result{system.MovieSystem().ValidateMovie(path.toStdString(), program_id)};
    QMessageBox::StandardButton answer;
    switch (result) {
    case Movie::ValidationResult::RevisionDismatch:
        answer = QMessageBox::question(
            this, "Revision Dismatch",
            "The movie file you're trying to load was created on a different revision of Citra."
            "<br/>Citra has had some changes during the time, and the playback may desync or not "
            "work as expected."
            "<br/><br/>Are you sure you still want to load the movie file?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return false;
        break;
    case Movie::ValidationResult::ProgramDismatch:
        answer = QMessageBox::question(
            this, "Program Dismatch",
            "The movie file you're trying to load was recorded with a different program."
            "<br/>The playback may not work as expected, and it may cause unexpected results."
            "<br/><br/>Are you sure you still want to load the movie file?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return false;
        break;
    case Movie::ValidationResult::Invalid:
        QMessageBox::critical(
            this, "Invalid Movie File",
            "The movie file you're trying to load is invalid."
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
        auto answer{QMessageBox::warning(
            this, "Play Movie",
            "To keep consistency with the RNG, it is recommended to play the movie from program "
            "start.<br>Are you sure you still want to play movies now?",
            QMessageBox::Yes | QMessageBox::No)};
        if (answer == QMessageBox::No)
            return;
    }
    const auto path{QFileDialog::getOpenFileName(this, "Play Movie", UISettings::values.movies_dir,
                                                 "Citra TAS Movie (*.ctm)")};
    if (path.isEmpty())
        return;
    UISettings::values.movies_dir = QFileInfo(path).path();
    auto& movie{system.MovieSystem()};
    if (system.IsPoweredOn()) {
        if (!ValidateMovie(path))
            return;
    } else {
        u64 program_id{movie.GetMovieProgramID(path.toStdString())};
        if (!program_id) {
            QMessageBox::critical(this, "Invalid Movie File",
                                  "The movie file you are trying to load is invalid."
                                  "<br/>Either the file is corrupted, or Citra has had made "
                                  "some major changes to the "
                                  "Movie module."
                                  "<br/>Please choose a different movie file and try again.");
            return;
        }
        auto program_path{program_list->FindProgramByProgramID(program_id)};
        if (program_path.isEmpty()) {
            const int num_recent_files{
                std::min(UISettings::values.recent_files.size(), MaxRecentFiles)};
            for (int i{}; i < num_recent_files; i++) {
                auto action_path{actions_recent_files[i]->data().toString()};
                if (!action_path.isEmpty()) {
                    if (QFile::exists(path)) {
                        auto loader{Loader::GetLoader(system, action_path.toStdString())};
                        u64 program_id_file;
                        if (loader->ReadProgramID(program_id_file) ==
                                Loader::ResultStatus::Success &&
                            program_id_file == program_id)
                            program_path = action_path;
                    }
                }
            }
            if (program_path.isEmpty()) {
                QMessageBox::warning(
                    this, "Program Not Found",
                    "The movie you're trying to play is from a program that isn't "
                    "in the program list and isn't in the recent files. If you have "
                    "the program, add the folder containing it to the program list or open "
                    "the program and try to play the movie again.");
                return;
            }
        }
        if (!ValidateMovie(path, program_id))
            return;
        movie.PrepareForPlayback(path.toStdString());
        BootProgram(program_path.toStdString());
    }
    movie.StartPlayback(path.toStdString(),
                        [this] { QMetaObject::invokeMethod(this, "OnMoviePlaybackCompleted"); });
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
        auto& movie{system.MovieSystem()};
        const bool was_recording{movie.IsRecordingInput()};
        movie.Shutdown();
        if (was_recording)
            QMessageBox::information(this, "Movie Saved", "The movie is successfully saved.");
    }
    ui.action_Record_Movie->setEnabled(true);
    ui.action_Play_Movie->setEnabled(true);
    ui.action_Stop_Recording_Playback->setEnabled(false);
}

void GMainWindow::OnCaptureScreenshot() {
    OnPauseProgram();
    const auto path{QFileDialog::getSaveFileName(
        this, "Capture Screenshot", UISettings::values.screenshots_dir, "PNG Image (*.png)")};
    OnStartProgram();
    if (path.isEmpty())
        return;
    UISettings::values.screenshots_dir = QFileInfo(path).path();
    screens->CaptureScreenshot(path);
}

void GMainWindow::OnDumpRAM() {
    const auto path{QFileDialog::getSaveFileName(this, "Dump RAM", UISettings::values.ram_dumps_dir,
                                                 "RAM Dump (*.bin)")};
    if (path.isEmpty())
        return;
    LOG_INFO(Frontend, "Dumping memory...");
    OnPauseProgram();
    UISettings::values.ram_dumps_dir = QFileInfo(path).path();
    FileUtil::IOFile file{path.toStdString(), "wb"};
    auto& memory{system.Memory()};
    file.WriteBytes(memory.fcram.data(), memory.fcram.size());
    OnStartProgram();
    LOG_INFO(Frontend, "Memory dump finished.");
}

void GMainWindow::UpdateStatusBar() {
    if (!emu_thread) {
        status_bar_update_timer.stop();
        return;
    }
    auto results{system.GetAndResetPerfStats()};
    if (Settings::values.use_frame_limit)
        emu_speed_label->setText(QString("Speed: %1% / %2%")
                                     .arg(results.emulation_speed * 100.0, 0, 'f', 0)
                                     .arg(Settings::values.frame_limit));
    else
        emu_speed_label->setText(
            QString("Speed: %1%").arg(results.emulation_speed * 100.0, 0, 'f', 0));
    fps_label->setText(QString("FPS: %1").arg(results.program_fps, 0, 'f', 0));
    emu_frametime_label->setText(
        QString("Frame: %1 ms").arg(results.frametime * 1000.0, 0, 'f', 2));
    emu_speed_label->setVisible(true);
    fps_label->setVisible(true);
    emu_frametime_label->setVisible(true);
}

void GMainWindow::OnCoreError(Core::System::ResultStatus result, const std::string& details) {
    QString message, title, status_message;
    switch (result) {
    case Core::System::ResultStatus::ErrorSystemFiles: {
        const QString common_message{
            "%1 is missing. Please <a "
            "href='https://github.com/valentinvanelslande/citra/wiki/"
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
    auto continue_button{message_box.addButton("Continue", QMessageBox::RejectRole)};
    auto abort_button{message_box.addButton("Abort", QMessageBox::AcceptRole)};
    if (result != Core::System::ResultStatus::ShutdownRequested)
        message_box.exec();
    if (result == Core::System::ResultStatus::ShutdownRequested ||
        message_box.clickedButton() == abort_button) {
        if (emu_thread) {
            ShutdownProgram();
            if (!system.set_program_file_path.empty()) {
                BootProgram(system.set_program_file_path);
                system.set_program_file_path.clear();
            }
        }
    } else {
        // Only show the message if the program is still running.
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
    if (!emu_thread || !UISettings::values.confirm_close)
        return true;
    auto answer{QMessageBox::question(this, "Citra", "Are you sure you want to close Citra?",
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
    program_list->SaveInterfaceLayout();
    hotkey_registry.SaveHotkeys();
    // Shutdown session if the emu thread is active...
    if (emu_thread)
        ShutdownProgram();
    screens->close();
    multiplayer_state->Close();
    QWidget::closeEvent(event);
}

static bool IsSingleFileDropEvent(const QMimeData* mime_data) {
    return mime_data->hasUrls() && mime_data->urls().length() == 1;
}

void GMainWindow::dropEvent(QDropEvent* event) {
    const auto mime_data{event->mimeData()};
    if (IsSingleFileDropEvent(mime_data) && ConfirmChangeProgram()) {
        auto filename{mime_data->urls().at(0).toLocalFile()};
        BootProgram(filename.toStdString());
    }
}

void GMainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (IsSingleFileDropEvent(event->mimeData()))
        event->acceptProposedAction();
}

void GMainWindow::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

bool GMainWindow::ConfirmChangeProgram() {
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
    auto theme_paths{default_theme_paths};
    if (UISettings::values.theme != UISettings::themes[0].second &&
        !UISettings::values.theme.isEmpty()) {
        const auto theme_uri{":" + UISettings::values.theme + "/style.qss"};
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
    ui.action_Screen_Layout_Swap_Screens->setChecked(Settings::values.swap_screens);
    ui.action_Screen_Layout_Custom_Layout->setChecked(Settings::values.custom_layout);
}

void GMainWindow::InitializeDiscordRPC() {
#ifdef ENABLE_DISCORD_RPC
    DiscordEventHandlers handlers{};
    handlers.disconnected = HandleDiscordDisconnected;
    handlers.errored = HandleDiscordError;
    Discord_Initialize("519577000537489408", &handlers, 0, nullptr);
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
        DiscordRichPresence presence{};
        auto& member{system.RoomMember()};
        if (member.IsConnected()) {
            const auto& member_info{member.GetMemberInformation()};
            presence.partySize = member_info.size();
            presence.state = info.name.c_str();
        }
        auto details{
            !short_title.empty()
                ? fmt::format("{} | {}-{}", short_title, Common::g_scm_branch, Common::g_scm_desc)
                : fmt::format("{}-{}", Common::g_scm_branch, Common::g_scm_desc)};
        presence.details = details.c_str();
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
    auto& system{Core::System::GetInstance()};
    // Load settings
    Config config{system};
    Log::Filter log_filter;
    log_filter.ParseFilterString(Settings::values.log_filter);
    Log::SetGlobalFilter(log_filter);
    Log::AddBackend(std::make_unique<Log::FileBackend>(
        FileUtil::GetUserPath(FileUtil::UserPath::UserDir) + LOG_FILE));
#ifdef _WIN32
    Log::AddBackend(std::make_unique<Log::DebuggerBackend>());
#endif
    ToggleConsole();
    config.LogErrors();
    Settings::LogSettings();
    // Initialize network and movie system
    system.Init1();
    // Register types to use in slots and signals
    qRegisterMetaType<std::size_t>("std::size_t");
    qRegisterMetaType<std::string>("std::string");
    qRegisterMetaType<Core::System::ResultStatus>("Core::System::ResultStatus");
    qRegisterMetaType<Service::AM::InstallStatus>("Service::AM::InstallStatus");
    // Create application
    QCoreApplication::setOrganizationName("Citra Valentin team");
    QCoreApplication::setApplicationName("Citra");
    QApplication app{argc, argv};
    // Qt changes the locale and causes issues in float conversion using std::to_string() when
    // generating shaders
    setlocale(LC_ALL, "C");
    // Create the window
    GMainWindow window{config, system};
    // Register camera factories
    Camera::RegisterFactory("image", std::make_unique<Camera::StillImageCameraFactory>());
    Camera::RegisterFactory("qt", std::make_unique<Camera::QtMultimediaCameraFactory>());
    Camera::QtMultimediaCameraHandler::Init();
    // Log version
    LOG_INFO(Frontend, "Citra version: Valentin {}-{}", Common::g_scm_branch, Common::g_scm_desc);
    window.show();
    return app.exec();
}
