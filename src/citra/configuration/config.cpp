// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <QSettings>
#include "citra/configuration/config.h"
#include "citra/ui_settings.h"
#include "common/file_util.h"
#include "input_common/main.h"
#include "input_common/udp/client.h"
#include "network/room.h"

const std::array<int, Settings::NativeButton::NumButtons> Config::default_buttons{{
    Qt::Key_A,
    Qt::Key_S,
    Qt::Key_Z,
    Qt::Key_X,
    Qt::Key_T,
    Qt::Key_G,
    Qt::Key_F,
    Qt::Key_H,
    Qt::Key_Q,
    Qt::Key_W,
    Qt::Key_M,
    Qt::Key_N,
    Qt::Key_1,
    Qt::Key_2,
    Qt::Key_B,
}};

const std::array<std::array<int, 5>, Settings::NativeAnalog::NumAnalogs> Config::default_analogs{{
    {
        Qt::Key_Up,
        Qt::Key_Down,
        Qt::Key_Left,
        Qt::Key_Right,
        Qt::Key_D,
    },
    {
        Qt::Key_I,
        Qt::Key_K,
        Qt::Key_J,
        Qt::Key_L,
        Qt::Key_D,
    },
}};

Config::Config(Core::System& system) : system{system} {
    auto path{fmt::format("{}qt-config.ini", FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir))};
    FileUtil::CreateFullPath(path);
    settings = std::make_unique<QSettings>(QString::fromStdString(path), QSettings::IniFormat);
    Load();
    // To apply default value changes
    Save();
    Settings::Apply(system);
}

Config::~Config() {
    Save();
}

void Config::Load() {
    settings->beginGroup("ControlPanel");
    Settings::values.volume = ReadSetting("volume", 1).toFloat();
    Settings::values.headphones_connected = ReadSetting("headphones_connected", false).toBool();
    Settings::values.factor_3d = ReadSetting("factor_3d", 0).toInt();
    Settings::values.p_adapter_connected = ReadSetting("p_adapter_connected", true).toBool();
    Settings::values.p_battery_charging = ReadSetting("p_battery_charging", true).toBool();
    Settings::values.p_battery_level = static_cast<u32>(ReadSetting("p_battery_level", 5).toInt());
    Settings::values.n_wifi_status = static_cast<u32>(ReadSetting("n_wifi_status", 0).toInt());
    Settings::values.n_wifi_link_level =
        static_cast<u8>(ReadSetting("n_wifi_link_level", 0).toInt());
    Settings::values.n_state = static_cast<u8>(ReadSetting("n_state", 0).toInt());
    settings->endGroup();
    settings->beginGroup("Controls");
    int size{settings->beginReadArray("profiles")};
    for (int p{}; p < size; ++p) {
        settings->setArrayIndex(p);
        Settings::ControllerProfile profile;
        profile.name = ReadSetting("name", "default").toString().toStdString();
        for (int i{}; i < Settings::NativeButton::NumButtons; ++i) {
            std::string default_param{InputCommon::GenerateKeyboardParam(default_buttons[i])};
            profile.buttons[i] = settings
                                     ->value(Settings::NativeButton::mapping[i],
                                             QString::fromStdString(default_param))
                                     .toString()
                                     .toStdString();
            if (profile.buttons[i].empty())
                profile.buttons[i] = default_param;
        }
        for (int i{}; i < Settings::NativeAnalog::NumAnalogs; ++i) {
            std::string default_param{InputCommon::GenerateAnalogParamFromKeys(
                default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
                default_analogs[i][3], default_analogs[i][4], 0.5f)};
            profile.analogs[i] = settings
                                     ->value(Settings::NativeAnalog::mapping[i],
                                             QString::fromStdString(default_param))
                                     .toString()
                                     .toStdString();
            if (profile.analogs[i].empty())
                profile.analogs[i] = default_param;
        }
        profile.motion_device =
            settings
                ->value("motion_device",
                        "engine:motion_emu,update_period:100,sensitivity:0.01,tilt_clamp:90.0")
                .toString()
                .toStdString();
        profile.touch_device =
            ReadSetting("touch_device", "engine:emu_window").toString().toStdString();
        profile.udp_input_address =
            ReadSetting("udp_input_address", InputCommon::CemuhookUDP::DEFAULT_ADDR)
                .toString()
                .toStdString();
        profile.udp_input_port = static_cast<u16>(
            ReadSetting("udp_input_port", InputCommon::CemuhookUDP::DEFAULT_PORT).toInt());
        profile.udp_pad_index = static_cast<u8>(ReadSetting("udp_pad_index", 0).toUInt());
        Settings::values.profiles.push_back(profile);
    }
    settings->endArray();
    Settings::values.profile = ReadSetting("profile", 0).toInt();
    if (size == 0) {
        Settings::ControllerProfile profile{};
        profile.name = "default";
        for (int i{}; i < Settings::NativeButton::NumButtons; ++i) {
            std::string default_param{InputCommon::GenerateKeyboardParam(default_buttons[i])};
            profile.buttons[i] = settings
                                     ->value(Settings::NativeButton::mapping[i],
                                             QString::fromStdString(default_param))
                                     .toString()
                                     .toStdString();
            if (profile.buttons[i].empty())
                profile.buttons[i] = default_param;
        }
        for (int i{}; i < Settings::NativeAnalog::NumAnalogs; ++i) {
            std::string default_param{InputCommon::GenerateAnalogParamFromKeys(
                default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
                default_analogs[i][3], default_analogs[i][4], 0.5f)};
            profile.analogs[i] = settings
                                     ->value(Settings::NativeAnalog::mapping[i],
                                             QString::fromStdString(default_param))
                                     .toString()
                                     .toStdString();
            if (profile.analogs[i].empty())
                profile.analogs[i] = default_param;
        }
        profile.motion_device =
            settings
                ->value("motion_device",
                        "engine:motion_emu,update_period:100,sensitivity:0.01,tilt_clamp:90.0")
                .toString()
                .toStdString();
        profile.touch_device =
            ReadSetting("touch_device", "engine:emu_window").toString().toStdString();
        profile.udp_input_address =
            ReadSetting("udp_input_address", InputCommon::CemuhookUDP::DEFAULT_ADDR)
                .toString()
                .toStdString();
        profile.udp_input_port = static_cast<u16>(
            ReadSetting("udp_input_port", InputCommon::CemuhookUDP::DEFAULT_PORT).toInt());
        profile.udp_pad_index = static_cast<u8>(ReadSetting("udp_pad_index", 0).toUInt());
        Settings::values.profiles.push_back(profile);
    }
    if (Settings::values.profile >= size) {
        Settings::values.profile = 0;
        errors.push_back("Invalid profile index");
    }
    Settings::LoadProfile(Settings::values.profile);
    settings->endGroup();
    settings->beginGroup("Core");
    Settings::values.keyboard_mode =
        static_cast<Settings::KeyboardMode>(ReadSetting("keyboard_mode", 1).toInt());
    Settings::values.enable_ns_launch = ReadSetting("enable_ns_launch", false).toBool();
    settings->endGroup();
    settings->beginGroup("LLE");
    for (const auto& service_module : Service::service_module_map) {
        bool use_lle{ReadSetting(QString::fromStdString(service_module.name), false).toBool()};
        Settings::values.lle_modules.emplace(service_module.name, use_lle);
    }
    Settings::values.use_lle_applets = ReadSetting("use_lle_applets", false).toBool();
    Settings::values.use_lle_dsp = ReadSetting("use_lle_dsp", false).toBool();
    settings->endGroup();
    settings->beginGroup("Graphics");
    Settings::values.enable_shadows = ReadSetting("enable_shadows", true).toBool();
    Settings::values.use_frame_limit = ReadSetting("use_frame_limit", true).toBool();
    Settings::values.frame_limit = ReadSetting("frame_limit", 100).toInt();
    Settings::values.screen_refresh_rate = ReadSetting("screen_refresh_rate", 60).toInt();
    Settings::values.min_vertices_per_thread = ReadSetting("min_vertices_per_thread", 10).toInt();
    u16 resolution_factor{static_cast<u16>(ReadSetting("resolution_factor", 1).toInt())};
    if (resolution_factor == 0)
        resolution_factor = 1;
    Settings::values.resolution_factor = resolution_factor;
#ifdef __APPLE__
    // Hardware shaders are broken on macOS thanks to poor drivers.
    // We still want to provide this option for test/development purposes, but disable it by
    // default.
    Settings::values.use_hw_shaders = ReadSetting("use_hw_shaders", false).toBool();
#else
    Settings::values.use_hw_shaders = ReadSetting("use_hw_shaders", true).toBool();
#endif
    Settings::values.shaders_accurate_gs = ReadSetting("shaders_accurate_gs", true).toBool();
    Settings::values.shaders_accurate_mul = ReadSetting("shaders_accurate_mul", false).toBool();
    Settings::values.bg_red = ReadSetting("bg_red", 0.0).toFloat();
    Settings::values.bg_green = ReadSetting("bg_green", 0.0).toFloat();
    Settings::values.bg_blue = ReadSetting("bg_blue", 0.0).toFloat();
    Settings::values.enable_cache_clear = ReadSetting("enable_cache_clear", false).toBool();
    settings->endGroup();
    settings->beginGroup("Layout");
    Settings::values.layout_option =
        static_cast<Settings::LayoutOption>(ReadSetting("layout_option").toInt());
    Settings::values.swap_screens = ReadSetting("swap_screens", false).toBool();
    Settings::values.custom_layout = ReadSetting("custom_layout", false).toBool();
    Settings::values.custom_top_left = ReadSetting("custom_top_left", 0).toInt();
    Settings::values.custom_top_top = ReadSetting("custom_top_top", 0).toInt();
    Settings::values.custom_top_right = ReadSetting("custom_top_right", 400).toInt();
    Settings::values.custom_top_bottom = ReadSetting("custom_top_bottom", 240).toInt();
    Settings::values.custom_bottom_left = ReadSetting("custom_bottom_left", 40).toInt();
    Settings::values.custom_bottom_top = ReadSetting("custom_bottom_top", 240).toInt();
    Settings::values.custom_bottom_right = ReadSetting("custom_bottom_right", 360).toInt();
    Settings::values.custom_bottom_bottom = ReadSetting("custom_bottom_bottom", 480).toInt();
    settings->endGroup();
    settings->beginGroup("Audio");
    Settings::values.enable_audio_stretching =
        ReadSetting("enable_audio_stretching", true).toBool();
    Settings::values.output_device = ReadSetting("output_device", "auto").toString().toStdString();
    settings->endGroup();
    using namespace Service::CAM;
    settings->beginGroup("Camera");
    Settings::values.camera_name[OuterRightCamera] =
        ReadSetting("camera_outer_right_name", "blank").toString().toStdString();
    Settings::values.camera_config[OuterRightCamera] =
        ReadSetting("camera_outer_right_config", "").toString().toStdString();
    Settings::values.camera_flip[OuterRightCamera] =
        ReadSetting("camera_outer_right_flip", 0).toInt();
    Settings::values.camera_name[InnerCamera] =
        ReadSetting("camera_inner_name", "blank").toString().toStdString();
    Settings::values.camera_config[InnerCamera] =
        ReadSetting("camera_inner_config", "").toString().toStdString();
    Settings::values.camera_flip[InnerCamera] = ReadSetting("camera_inner_flip", 0).toInt();
    Settings::values.camera_name[OuterLeftCamera] =
        ReadSetting("camera_outer_left_name", "blank").toString().toStdString();
    Settings::values.camera_config[OuterLeftCamera] =
        ReadSetting("camera_outer_left_config", "").toString().toStdString();
    Settings::values.camera_flip[OuterLeftCamera] =
        ReadSetting("camera_outer_left_flip", 0).toInt();
    settings->endGroup();
    settings->beginGroup("Data Storage");
    Settings::values.use_virtual_sd = ReadSetting("use_virtual_sd", true).toBool();
    Settings::values.nand_dir = ReadSetting("nand_dir", "").toString().toStdString();
    Settings::values.sdmc_dir = ReadSetting("sdmc_dir", "").toString().toStdString();
    settings->endGroup();
    settings->beginGroup("System");
    Settings::values.region_value =
        ReadSetting("region_value", Settings::REGION_VALUE_AUTO_SELECT).toInt();
    Settings::values.init_clock = static_cast<Settings::InitClock>(
        ReadSetting("init_clock", static_cast<u32>(Settings::InitClock::SystemTime)).toInt());
    Settings::values.init_time = ReadSetting("init_time", 946681277ULL).toULongLong();
    settings->endGroup();
    settings->beginGroup("Miscellaneous");
    Settings::values.log_filter = ReadSetting("log_filter", "*:Info").toString().toStdString();
    settings->endGroup();
    settings->beginGroup("Hacks");
    Settings::values.priority_boost = ReadSetting("priority_boost", false).toBool();
    Settings::values.ticks_mode =
        static_cast<Settings::TicksMode>(ReadSetting("ticks_mode", 0).toInt());
    Settings::values.ticks = ReadSetting("ticks", 0).toULongLong();
    Settings::values.ignore_format_reinterpretation =
        ReadSetting("ignore_format_reinterpretation", false).toBool();
    Settings::values.force_memory_mode_7 = ReadSetting("force_memory_mode_7", false).toBool();
    Settings::values.disable_mh_2xmsaa = ReadSetting("disable_mh_2xmsaa", false).toBool();
    settings->endGroup();
    settings->beginGroup("UI");
    UISettings::values.confirm_close = ReadSetting("confirm_close", true).toBool();
    UISettings::values.enable_discord_rpc = ReadSetting("enable_discord_rpc", true).toBool();
    UISettings::values.theme = ReadSetting("theme", UISettings::themes[0].second).toString();
    settings->beginGroup("UILayout");
    UISettings::values.geometry = ReadSetting("geometry").toByteArray();
    UISettings::values.state = ReadSetting("state").toByteArray();
    UISettings::values.screens_geometry = ReadSetting("geometryScreens").toByteArray();
    UISettings::values.programlist_header_state =
        ReadSetting("programListHeaderState").toByteArray();
    UISettings::values.configuration_geometry = ReadSetting("configurationGeometry").toByteArray();
    settings->endGroup();
    settings->beginGroup("ProgramList");
    int icon_size{ReadSetting("iconSize", 2).toInt()};
    if (icon_size < 0 || icon_size > 2)
        icon_size = 2;
    UISettings::values.program_list_icon_size = UISettings::ProgramListIconSize{icon_size};
    int row_1{ReadSetting("row1", 2).toInt()};
    if (row_1 < 0 || row_1 > 4)
        row_1 = 2;
    UISettings::values.program_list_row_1 = UISettings::ProgramListText{row_1};
    int row_2{ReadSetting("row2", 0).toInt()};
    if (row_2 < -1 || row_2 > 4)
        row_2 = 0;
    UISettings::values.program_list_row_2 = UISettings::ProgramListText{row_2};
    UISettings::values.program_list_hide_no_icon = ReadSetting("hideNoIcon", false).toBool();
    settings->endGroup();
    settings->beginGroup("Paths");
    UISettings::values.amiibo_dir = ReadSetting("amiibo_dir", ".").toString();
    UISettings::values.programs_dir = ReadSetting("programs_dir", ".").toString();
    UISettings::values.movies_dir = ReadSetting("movies_dir", ".").toString();
    UISettings::values.ram_dumps_dir = ReadSetting("ram_dumps_dir", ".").toString();
    UISettings::values.screenshots_dir = ReadSetting("screenshots_dir", ".").toString();
    UISettings::values.seeds_dir = ReadSetting("seeds_dir", ".").toString();
    size = settings->beginReadArray("appdirs");
    for (int i{}; i < size; ++i) {
        settings->setArrayIndex(i);
        UISettings::AppDir program_dir;
        program_dir.path = ReadSetting("path").toString();
        program_dir.deep_scan = ReadSetting("deep_scan", false).toBool();
        program_dir.expanded = ReadSetting("expanded", true).toBool();
        UISettings::values.program_dirs.append(program_dir);
    }
    settings->endArray();
    // Create NAND and SD card directories if empty, these aren't removable through the UI
    if (UISettings::values.program_dirs.isEmpty()) {
        UISettings::AppDir program_dir{};
        program_dir.path = "INSTALLED";
        program_dir.expanded = true;
        UISettings::values.program_dirs.append(program_dir);
        program_dir.path = "SYSTEM";
        UISettings::values.program_dirs.append(program_dir);
    }
    UISettings::values.recent_files = ReadSetting("recentFiles").toStringList();
    settings->endGroup();
    settings->beginGroup("Shortcuts");
    const std::array<UISettings::Shortcut, 21> default_hotkeys{{
        {"Load File", "Main Window", UISettings::ContextualShortcut{"CTRL+O", Qt::WindowShortcut}},
        {"Exit Citra", "Main Window", UISettings::ContextualShortcut{"Ctrl+Q", Qt::WindowShortcut}},
        {"Continue/Pause Emulation", "Main Window",
         UISettings::ContextualShortcut{"F4", Qt::WindowShortcut}},
        {"Stop Emulation", "Main Window", UISettings::ContextualShortcut{"F5", Qt::WindowShortcut}},
        {"Restart Emulation", "Main Window",
         UISettings::ContextualShortcut{"F6", Qt::WindowShortcut}},
        {"Swap Screens", "Main Window", UISettings::ContextualShortcut{"F9", Qt::WindowShortcut}},
        {"Toggle Screen Layout", "Main Window",
         UISettings::ContextualShortcut{"F10", Qt::WindowShortcut}},
        {"Toggle Filter Bar", "Main Window",
         UISettings::ContextualShortcut{"Ctrl+F", Qt::WindowShortcut}},
        {"Toggle Status Bar", "Main Window",
         UISettings::ContextualShortcut{"Ctrl+S", Qt::WindowShortcut}},
        {"Fullscreen", "Main Window",
         UISettings::ContextualShortcut{"CTRL+F11", Qt::WindowShortcut}},
        {"Exit Fullscreen", "Main Window",
         UISettings::ContextualShortcut{"Escape", Qt::WindowShortcut}},
        {"Toggle Speed Limit", "Main Window",
         UISettings::ContextualShortcut{"Ctrl+Z", Qt::ApplicationShortcut}},
        {"Increase Speed Limit", "Main Window",
         UISettings::ContextualShortcut{"+", Qt::ApplicationShortcut}},
        {"Decrease Speed Limit", "Main Window",
         UISettings::ContextualShortcut{"-", Qt::ApplicationShortcut}},
        {"Advance Frame", "Main Window",
         UISettings::ContextualShortcut{"\\", Qt::ApplicationShortcut}},
        {"Toggle Frame Advancing", "Main Window",
         UISettings::ContextualShortcut{"Ctrl+A", Qt::ApplicationShortcut}},
        {"Load Amiibo", "Main Window",
         UISettings::ContextualShortcut{"F2", Qt::ApplicationShortcut}},
        {"Remove Amiibo", "Main Window",
         UISettings::ContextualShortcut{"F3", Qt::ApplicationShortcut}},
        {"Capture Screenshot", "Main Window",
         UISettings::ContextualShortcut{"Ctrl+P", Qt::ApplicationShortcut}},
        {"Toggle Sleep Mode", "Main Window",
         UISettings::ContextualShortcut{"F7", Qt::ApplicationShortcut}},
        {"Change CPU Ticks", "Main Window",
         UISettings::ContextualShortcut{"CTRL+T", Qt::ApplicationShortcut}},
    }};
    for (int i{}; i < default_hotkeys.size(); i++) {
        settings->beginGroup(default_hotkeys[i].group);
        settings->beginGroup(default_hotkeys[i].name);
        UISettings::values.shortcuts.push_back(
            {default_hotkeys[i].name, default_hotkeys[i].group,
             UISettings::ContextualShortcut(
                 settings->value("KeySeq", default_hotkeys[i].shortcut.first).toString(),
                 settings->value("Context", default_hotkeys[i].shortcut.second).toInt())});
        settings->endGroup();
        settings->endGroup();
    }
    settings->endGroup();
    UISettings::values.fullscreen = ReadSetting("fullscreen", false).toBool();
    UISettings::values.show_filter_bar = ReadSetting("showFilterBar", true).toBool();
    UISettings::values.show_status_bar = ReadSetting("showStatusBar", true).toBool();
    UISettings::values.show_console = ReadSetting("showConsole", false).toBool();
    settings->beginGroup("Multiplayer");
    UISettings::values.direct_connect_nickname =
        ReadSetting("direct_connect_nickname", "").toString();
    UISettings::values.lobby_nickname = ReadSetting("lobby_nickname", "").toString();
    UISettings::values.room_nickname = ReadSetting("room_nickname", "").toString();
    UISettings::values.ip = ReadSetting("ip", "").toString();
    UISettings::values.port = ReadSetting("port", Network::DefaultRoomPort).toUInt();
    UISettings::values.room_name = ReadSetting("room_name", "").toString();
    UISettings::values.room_port = ReadSetting("room_port", Network::DefaultRoomPort).toUInt();
    bool ok{};
    UISettings::values.host_type = ReadSetting("host_type", 0).toUInt(&ok);
    if (!ok)
        UISettings::values.host_type = 0;
    UISettings::values.room_description = ReadSetting("room_description", "").toString();
    auto list{ReadSetting("ban_list", QStringList()).toStringList()};
    for (auto s : list)
        UISettings::values.ban_list.push_back(s.toStdString());
    settings->endGroup();
    settings->endGroup();
}

void Config::LogErrors() {
    for (const auto& error : errors)
        LOG_ERROR(Config, "{}", error);
    errors.clear();
}

void Config::Save() {
    settings->beginGroup("ControlPanel");
    WriteSetting("volume", Settings::values.volume, 1);
    WriteSetting("headphones_connected", Settings::values.headphones_connected, false);
    WriteSetting("factor_3d", Settings::values.factor_3d, 0);
    WriteSetting("p_adapter_connected", Settings::values.p_adapter_connected, true);
    WriteSetting("p_battery_charging", Settings::values.p_battery_charging, true);
    WriteSetting("p_battery_level", Settings::values.p_battery_level, 5);
    WriteSetting("n_wifi_status", Settings::values.n_wifi_status, 0);
    WriteSetting("n_wifi_link_level", Settings::values.n_wifi_link_level, 0);
    WriteSetting("n_state", Settings::values.n_state, 0);
    settings->endGroup();
    settings->beginGroup("Controls");
    settings->beginWriteArray("profiles");
    for (int p{}; p < Settings::values.profiles.size(); ++p) {
        settings->setArrayIndex(p);
        const auto& profile{Settings::values.profiles[p]};
        WriteSetting("name", QString::fromStdString(profile.name));
        for (int i{}; i < Settings::NativeButton::NumButtons; ++i)
            WriteSetting(
                QString::fromStdString(Settings::NativeButton::mapping[i]),
                QString::fromStdString(profile.buttons[i]),
                QString::fromStdString(InputCommon::GenerateKeyboardParam(default_buttons[i])));
        for (int i{}; i < Settings::NativeAnalog::NumAnalogs; ++i)
            WriteSetting(QString::fromStdString(Settings::NativeAnalog::mapping[i]),
                         QString::fromStdString(profile.analogs[i]),
                         QString::fromStdString(InputCommon::GenerateAnalogParamFromKeys(
                             default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
                             default_analogs[i][3], default_analogs[i][4], 0.5f)));
        WriteSetting("motion_device", QString::fromStdString(profile.motion_device));
        WriteSetting("touch_device", QString::fromStdString(profile.touch_device));
        WriteSetting("udp_input_address", QString::fromStdString(profile.udp_input_address));
        WriteSetting("udp_input_port", profile.udp_input_port);
        WriteSetting("udp_pad_index", profile.udp_pad_index);
    }
    settings->endArray();
    WriteSetting("profile", Settings::values.profile);
    settings->endGroup();
    settings->beginGroup("Core");
    WriteSetting("keyboard_mode", static_cast<int>(Settings::values.keyboard_mode),
                 static_cast<int>(Settings::KeyboardMode::Qt));
    WriteSetting("enable_ns_launch", Settings::values.enable_ns_launch, false);
    settings->endGroup();
    settings->beginGroup("LLE");
    for (const auto& service_module : Settings::values.lle_modules)
        WriteSetting(QString::fromStdString(service_module.first), service_module.second, false);
    WriteSetting("use_lle_applets", Settings::values.use_lle_applets, false);
    WriteSetting("use_lle_dsp", Settings::values.use_lle_dsp, false);
    settings->endGroup();
    settings->beginGroup("Graphics");
    WriteSetting("enable_shadows", Settings::values.enable_shadows, true);
    WriteSetting("use_frame_limit", Settings::values.use_frame_limit, true);
    WriteSetting("frame_limit", Settings::values.frame_limit, 100);
    WriteSetting("screen_refresh_rate", Settings::values.screen_refresh_rate, 60);
    WriteSetting("min_vertices_per_thread", Settings::values.min_vertices_per_thread, 10);
    WriteSetting("resolution_factor", Settings::values.resolution_factor, 1);
    WriteSetting("use_hw_shaders", Settings::values.use_hw_shaders, true);
    WriteSetting("shaders_accurate_gs", Settings::values.shaders_accurate_gs, true);
    WriteSetting("shaders_accurate_mul", Settings::values.shaders_accurate_mul, false);
    // Cast to double because Qt's written float values aren't human-readable
    WriteSetting("bg_red", (double)Settings::values.bg_red, 0.0);
    WriteSetting("bg_green", (double)Settings::values.bg_green, 0.0);
    WriteSetting("bg_blue", (double)Settings::values.bg_blue, 0.0);
    WriteSetting("enable_cache_clear", Settings::values.enable_cache_clear, false);
    settings->endGroup();
    settings->beginGroup("Layout");
    WriteSetting("layout_option", static_cast<int>(Settings::values.layout_option));
    WriteSetting("swap_screens", Settings::values.swap_screens, false);
    WriteSetting("custom_layout", Settings::values.custom_layout, false);
    WriteSetting("custom_top_left", Settings::values.custom_top_left, 0);
    WriteSetting("custom_top_top", Settings::values.custom_top_top, 0);
    WriteSetting("custom_top_right", Settings::values.custom_top_right, 400);
    WriteSetting("custom_top_bottom", Settings::values.custom_top_bottom, 240);
    WriteSetting("custom_bottom_left", Settings::values.custom_bottom_left, 40);
    WriteSetting("custom_bottom_top", Settings::values.custom_bottom_top, 240);
    WriteSetting("custom_bottom_right", Settings::values.custom_bottom_right, 360);
    WriteSetting("custom_bottom_bottom", Settings::values.custom_bottom_bottom, 480);
    settings->endGroup();
    settings->beginGroup("Audio");
    WriteSetting("enable_audio_stretching", Settings::values.enable_audio_stretching, true);
    WriteSetting("output_device", QString::fromStdString(Settings::values.output_device), "auto");
    settings->endGroup();
    using namespace Service::CAM;
    settings->beginGroup("Camera");
    WriteSetting("camera_outer_right_name",
                 QString::fromStdString(Settings::values.camera_name[OuterRightCamera]), "blank");
    WriteSetting("camera_outer_right_config",
                 QString::fromStdString(Settings::values.camera_config[OuterRightCamera]), "");
    WriteSetting("camera_outer_right_flip", Settings::values.camera_flip[OuterRightCamera], 0);
    WriteSetting("camera_inner_name",
                 QString::fromStdString(Settings::values.camera_name[InnerCamera]), "blank");
    WriteSetting("camera_inner_config",
                 QString::fromStdString(Settings::values.camera_config[InnerCamera]), "");
    WriteSetting("camera_inner_flip", Settings::values.camera_flip[InnerCamera], 0);
    WriteSetting("camera_outer_left_name",
                 QString::fromStdString(Settings::values.camera_name[OuterLeftCamera]), "blank");
    WriteSetting("camera_outer_left_config",
                 QString::fromStdString(Settings::values.camera_config[OuterLeftCamera]), "");
    WriteSetting("camera_outer_left_flip", Settings::values.camera_flip[OuterLeftCamera], 0);
    settings->endGroup();
    settings->beginGroup("Data Storage");
    WriteSetting("use_virtual_sd", Settings::values.use_virtual_sd, true);
    WriteSetting("nand_dir", QString::fromStdString(Settings::values.nand_dir));
    WriteSetting("sdmc_dir", QString::fromStdString(Settings::values.sdmc_dir));
    settings->endGroup();
    settings->beginGroup("System");
    WriteSetting("region_value", Settings::values.region_value, Settings::REGION_VALUE_AUTO_SELECT);
    WriteSetting("init_clock", static_cast<u32>(Settings::values.init_clock),
                 static_cast<u32>(Settings::InitClock::SystemTime));
    WriteSetting("init_time", static_cast<unsigned long long>(Settings::values.init_time),
                 946681277ULL);
    settings->endGroup();
    settings->beginGroup("Miscellaneous");
    WriteSetting("log_filter", QString::fromStdString(Settings::values.log_filter), "*:Info");
    settings->endGroup();
    settings->beginGroup("Hacks");
    WriteSetting("priority_boost", Settings::values.priority_boost, false);
    WriteSetting("ticks_mode", static_cast<int>(Settings::values.ticks_mode), 0);
    WriteSetting("ticks", static_cast<unsigned long long>(Settings::values.ticks), 0);
    WriteSetting("ignore_format_reinterpretation", Settings::values.ignore_format_reinterpretation);
    WriteSetting("force_memory_mode_7", Settings::values.force_memory_mode_7);
    WriteSetting("disable_mh_2xmsaa", Settings::values.disable_mh_2xmsaa);
    settings->endGroup();
    settings->beginGroup("UI");
    WriteSetting("confirm_close", UISettings::values.confirm_close, true);
    WriteSetting("enable_discord_rpc", UISettings::values.enable_discord_rpc, true);
    WriteSetting("theme", UISettings::values.theme, UISettings::themes[0].second);
    settings->beginGroup("UILayout");
    WriteSetting("geometry", UISettings::values.geometry);
    WriteSetting("state", UISettings::values.state);
    WriteSetting("geometryScreens", UISettings::values.screens_geometry);
    WriteSetting("programListHeaderState", UISettings::values.programlist_header_state);
    WriteSetting("configurationGeometry", UISettings::values.configuration_geometry);
    settings->endGroup();
    settings->beginGroup("ProgramList");
    WriteSetting("iconSize", static_cast<int>(UISettings::values.program_list_icon_size));
    WriteSetting("row1", static_cast<int>(UISettings::values.program_list_row_1));
    WriteSetting("row2", static_cast<int>(UISettings::values.program_list_row_2));
    WriteSetting("hideNoIcon", UISettings::values.program_list_hide_no_icon, false);
    settings->endGroup();
    settings->beginGroup("Paths");
    WriteSetting("amiibo_dir", UISettings::values.amiibo_dir);
    WriteSetting("programs_dir", UISettings::values.programs_dir);
    WriteSetting("movies_dir", UISettings::values.movies_dir);
    WriteSetting("ram_dumps_dir", UISettings::values.ram_dumps_dir);
    WriteSetting("screenshots_dir", UISettings::values.screenshots_dir);
    WriteSetting("seeds_dir", UISettings::values.seeds_dir);
    settings->beginWriteArray("appdirs");
    for (int i{}; i < UISettings::values.program_dirs.size(); ++i) {
        settings->setArrayIndex(i);
        const auto& program_dir{UISettings::values.program_dirs.at(i)};
        WriteSetting("path", program_dir.path);
        WriteSetting("deep_scan", program_dir.deep_scan);
        WriteSetting("expanded", program_dir.expanded);
    }
    settings->endArray();
    WriteSetting("recentFiles", UISettings::values.recent_files);
    settings->endGroup();
    settings->beginGroup("Shortcuts");
    for (const auto& shortcut : UISettings::values.shortcuts) {
        settings->beginGroup(shortcut.group);
        settings->beginGroup(shortcut.name);
        WriteSetting("KeySeq", shortcut.shortcut.first);
        WriteSetting("Context", shortcut.shortcut.second);
        settings->endGroup();
        settings->endGroup();
    }
    settings->endGroup();
    WriteSetting("fullscreen", UISettings::values.fullscreen, false);
    WriteSetting("showFilterBar", UISettings::values.show_filter_bar, true);
    WriteSetting("showStatusBar", UISettings::values.show_status_bar, true);
    WriteSetting("showConsole", UISettings::values.show_console, false);
    settings->beginGroup("Multiplayer");
    WriteSetting("direct_connect_nickname", UISettings::values.direct_connect_nickname, "");
    WriteSetting("lobby_nickname", UISettings::values.lobby_nickname, "");
    WriteSetting("room_nickname", UISettings::values.room_nickname, "");
    WriteSetting("ip", UISettings::values.ip, "");
    WriteSetting("port", UISettings::values.port, Network::DefaultRoomPort);
    WriteSetting("room_name", UISettings::values.room_name, "");
    WriteSetting("room_port", UISettings::values.room_port, Network::DefaultRoomPort);
    WriteSetting("host_type", UISettings::values.host_type, 0);
    WriteSetting("room_description", UISettings::values.room_description, "");
    QStringList list;
    for (const auto& i : UISettings::values.ban_list)
        list.append(QString::fromStdString(i));
    WriteSetting("ban_list", list);
    settings->endGroup();
    settings->endGroup();
}

void Config::RestoreDefaults() {
    settings->clear();
    Settings::values.profiles.clear();
    Load();
    Save();
    Settings::Apply(system);
}

QVariant Config::ReadSetting(const QString& name) {
    return settings->value(name);
}

QVariant Config::ReadSetting(const QString& name, const QVariant& default_value) {
    QVariant result;
    if (settings->value(name + "/default", false).toBool())
        result = default_value;
    else
        result = settings->value(name, default_value);
    return result;
}

void Config::WriteSetting(const QString& name, const QVariant& value) {
    settings->setValue(name, value);
}

void Config::WriteSetting(const QString& name, const QVariant& value,
                          const QVariant& default_value) {
    settings->setValue(name + "/default", value == default_value);
    settings->setValue(name, value);
}
