// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <iostream>
#include <memory>
#include <regex>
#include <string>
#include <thread>
#include <getopt.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN32
// windows.h needs to be included before shellapi.h
#include <windows.h>

#include <shellapi.h>
#endif

#ifdef ENABLE_DISCORD_RPC
#include <discord_rpc.h>
#endif
#include "citra/config.h"
#include "citra/emu_window/emu_window_sdl2.h"
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/filter.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "common/scope_exit.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/cia_container.h"
#include "core/hle/service/am/am.h"
#include "core/loader/loader.h"
#include "core/movie.h"
#include "core/settings.h"
#include "network/network.h"

#ifdef _WIN32
extern "C" {
// tells Nvidia drivers to use the dedicated GPU by default on laptops with switchable graphics
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
}
#endif

#ifdef ENABLE_DISCORD_RPC
static void HandleDiscordDisconnected(int errorCode, const char* message) {
    LOG_ERROR(Frontend, "Disconnected, error: {} ({})", message, errorCode);
}

static void HandleDiscordError(int errorCode, const char* message) {
    LOG_ERROR(Frontend, "Error: {} ({})", message, errorCode);
}
#endif

static void PrintHelp(const char* argv0) {
    std::cout << "Usage: " << argv0
              << " [options] <filename>\n"
                 "-i, --install=FILE    Installs a specified CIA file\n"
                 "-m, --multiplayer=nick:password@address:port"
                 " Nickname, password, address and port for multiplayer\n"
                 "-r, --movie-record=[file]  Record a movie (game inputs) to the given file\n"
                 "-p, --movie-play=[file]    Playback the movie (game inputs) from the given file\n"
                 "-f, --fullscreen     Start in fullscreen mode\n"
                 "-h, --help           Display this help and exit\n"
                 "-v, --version        Output version information and exit\n";
}

static void PrintVersion() {
    std::cout << "Citra " << Common::g_build_version << std::endl;
}

static void OnStateChanged(const Network::RoomMember::State& state) {
    switch (state) {
    case Network::RoomMember::State::Idle:
        LOG_DEBUG(Network, "Network is idle");
        break;
    case Network::RoomMember::State::Joining:
        LOG_DEBUG(Network, "Connection sequence to room started");
        break;
    case Network::RoomMember::State::Joined:
        LOG_DEBUG(Network, "Successfully joined to the room");
        break;
    case Network::RoomMember::State::LostConnection:
        LOG_DEBUG(Network, "Lost connection to the room");
        break;
    case Network::RoomMember::State::CouldNotConnect:
        LOG_ERROR(Network, "State: CouldNotConnect");
        exit(1);
        break;
    case Network::RoomMember::State::NameCollision:
        LOG_ERROR(
            Network,
            "You tried to use the same nickname then another user that is connected to the Room");
        exit(1);
        break;
    case Network::RoomMember::State::MacCollision:
        LOG_ERROR(Network, "You tried to use the same MAC-Address then another user that is "
                           "connected to the Room");
        exit(1);
        break;
    case Network::RoomMember::State::WrongPassword:
        LOG_ERROR(Network, "Room replied with: Wrong password");
        exit(1);
        break;
    case Network::RoomMember::State::WrongVersion:
        LOG_ERROR(Network,
                  "You are using a different version then the room you are trying to connect to");
        exit(1);
        break;
    default:
        break;
    }
}

static void OnMessageReceived(const Network::ChatEntry& msg) {
    std::cout << std::endl << msg.nickname << ": " << msg.message << std::endl << std::endl;
}

/// Application entry point
int main(int argc, char** argv) {
    Config config;
    int option_index = 0;
    std::string movie_record;
    std::string movie_play;

#ifdef _WIN32
    int argc_w;
    auto argv_w = CommandLineToArgvW(GetCommandLineW(), &argc_w);

    if (argv_w == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to get command line arguments");
        return -1;
    }
#endif
    std::string filepath;

    bool use_multiplayer = false;
    bool fullscreen = false;
    std::string nickname{};
    std::string password{};
    std::string address{};
    u16 port = Network::DefaultRoomPort;

    static struct option long_options[] = {
        {"install", required_argument, 0, 'i'},
        {"multiplayer", required_argument, 0, 'm'},
        {"movie-record", required_argument, 0, 'r'},
        {"movie-play", required_argument, 0, 'p'},
        {"fullscreen", no_argument, 0, 'f'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0},
    };

    while (optind < argc) {
        char arg = getopt_long(argc, argv, "i:m:r:p:fhv", long_options, &option_index);
        if (arg != -1) {
            switch (arg) {
            case 'i': {
                const auto cia_progress = [](size_t written, size_t total) {
                    LOG_INFO(Frontend, "{:02d}%", (written * 100 / total));
                };
                if (Service::AM::InstallCIA(std::string(optarg), cia_progress) !=
                    Service::AM::InstallStatus::Success)
                    errno = EINVAL;
                if (errno != 0)
                    exit(1);
                break;
            }
            case 'm': {
                use_multiplayer = true;
                const std::string str_arg(optarg);
                // regex to check if the format is nickname:password@ip:port
                // with optional :password
                const std::regex re("^([^:]+)(?::(.+))?@([^:]+)(?::([0-9]+))?$");
                if (!std::regex_match(str_arg, re)) {
                    std::cout << "Wrong format for option --multiplayer\n";
                    PrintHelp(argv[0]);
                    return 0;
                }

                std::smatch match;
                std::regex_search(str_arg, match, re);
                ASSERT(match.size() == 5);
                nickname = match[1];
                password = match[2];
                address = match[3];
                if (!match[4].str().empty())
                    port = std::stoi(match[4]);
                std::regex nickname_re("^[a-zA-Z0-9._\\- ]+$");
                if (!std::regex_match(nickname, nickname_re)) {
                    std::cout
                        << "Nickname is not valid. Must be 4 to 20 alphanumeric characters.\n";
                    return 0;
                }
                if (address.empty()) {
                    std::cout << "Address to room must not be empty.\n";
                    return 0;
                }
                break;
            }
            case 'r':
                movie_record = optarg;
                break;
            case 'p':
                movie_play = optarg;
                break;
            case 'f':
                fullscreen = true;
                LOG_INFO(Frontend, "Starting in fullscreen mode...");
                break;
            case 'h':
                PrintHelp(argv[0]);
                return 0;
            case 'v':
                PrintVersion();
                return 0;
            }
        } else {
#ifdef _WIN32
            filepath = Common::UTF16ToUTF8(argv_w[optind]);
#else
            filepath = argv[optind];
#endif
            optind++;
        }
    }

#ifdef _WIN32
    LocalFree(argv_w);
#endif

    if (filepath.empty()) {
        LOG_CRITICAL(Frontend, "Failed to load ROM: No ROM specified");
        return -1;
    }

    if (!movie_record.empty() && !movie_play.empty()) {
        LOG_CRITICAL(Frontend, "Cannot both play and record a movie");
        return -1;
    }

    Log::Filter log_filter;
    log_filter.ParseFilterString(Settings::values.log_filter);
    Log::SetGlobalFilter(log_filter);

    Log::AddBackend(std::make_unique<Log::ColorConsoleBackend>());
    FileUtil::CreateFullPath(FileUtil::GetUserPath(D_LOGS_IDX));
    Log::AddBackend(
        std::make_unique<Log::FileBackend>(FileUtil::GetUserPath(D_LOGS_IDX) + LOG_FILE));

    std::unique_ptr<EmuWindow_SDL2> emu_window{std::make_unique<EmuWindow_SDL2>(fullscreen)};

    Core::System& system{Core::System::GetInstance()};

    SCOPE_EXIT({ system.Shutdown(); });

    const Core::System::ResultStatus load_result{system.Load(emu_window.get(), filepath)};

    switch (load_result) {
    case Core::System::ResultStatus::ErrorGetLoader:
        LOG_CRITICAL(Frontend, "Failed to obtain loader for {}!", filepath);
        return -1;
    case Core::System::ResultStatus::ErrorLoader:
        LOG_CRITICAL(Frontend, "Failed to load ROM!");
        return -1;
    case Core::System::ResultStatus::ErrorLoader_ErrorEncrypted:
        LOG_CRITICAL(Frontend,
                     "The game that you are trying to load must be decrypted before "
                     "being used with Citra. \n\n For more information on dumping and "
                     "decrypting games, please refer to: "
                     "https://github.com/valentinvanelslande/citra/wiki/Dumping-Game-Cartridges");
        return -1;
    case Core::System::ResultStatus::ErrorLoader_ErrorInvalidFormat:
        LOG_CRITICAL(Frontend, "Error while loading ROM: The ROM format is not supported.");
        return -1;
    case Core::System::ResultStatus::ErrorNotInitialized:
        LOG_CRITICAL(Frontend, "CPUCore not initialized");
        return -1;
    case Core::System::ResultStatus::ErrorSystemMode:
        LOG_CRITICAL(Frontend, "Failed to determine system mode!");
        return -1;
    case Core::System::ResultStatus::ErrorVideoCore:
        LOG_CRITICAL(Frontend, "VideoCore not initialized");
        return -1;
    case Core::System::ResultStatus::Success:
        break; // Expected case
    }

    if (use_multiplayer) {
        if (auto member = Network::GetRoomMember().lock()) {
            member->BindOnChatMessageRecieved(OnMessageReceived);
            member->BindOnStateChanged(OnStateChanged);
            LOG_DEBUG(Network, "Start connection to {}:{} with nickname {}", address, port,
                      nickname);
            member->Join(nickname, address.c_str(), port, 0, Network::NoPreferredMac, password);
        } else {
            LOG_ERROR(Network, "Could not access RoomMember");
            return 0;
        }
    }

#ifdef ENABLE_DISCORD_RPC
    std::string title;
    system.GetAppLoader().ReadTitle(title);
    DiscordEventHandlers handlers{};
    handlers.disconnected = HandleDiscordDisconnected;
    handlers.errored = HandleDiscordError;
    Discord_Initialize("472104565165260826", &handlers, 0, NULL);
    DiscordRichPresence presence{};
    presence.state = title.empty() ? "Unknown game" : title.c_str();
    presence.details = "Playing";
    presence.startTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
    presence.largeImageKey = "icon";
    Discord_UpdatePresence(&presence);
#endif

    if (!movie_play.empty()) {
        Core::Movie::GetInstance().StartPlayback(movie_play);
    }
    if (!movie_record.empty()) {
        Core::Movie::GetInstance().StartRecording(movie_record);
    }

    while (emu_window->IsOpen()) {
        system.RunLoop();
    }

    Core::Movie::GetInstance().Shutdown();

#ifdef ENABLE_DISCORD_RPC
    Discord_ClearPresence();
    Discord_Shutdown();
#endif
    return 0;
}
