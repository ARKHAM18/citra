// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <fmt/format.h>
#include <getopt.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN32
// windows.h needs to be included before shellapi.h
#include <windows.h>

#include <shellapi.h>
#endif

#include "common/common_types.h"
#include "common/file_util.h"
#include "common/scm_rev.h"
#include "common/string_util.h"
#include "network/room.h"

static void PrintHelp(const char* argv0) {
    std::cout << "Usage: " << argv0
              << " [options] <filename>\n"
                 "--room-name         The name of the room\n"
                 "--room-description  The room description\n"
                 "--port              The port used for the room\n"
                 "--max-members       The maximum number of members for this room\n"
                 "--announce          Create a public room\n"
                 "--password          The password for the room\n"
                 "--creator           The creator of the room\n"
                 "--ban-list-file     The file for storing the room ban list\n"
                 "-h, --help          Display this help and exit\n"
                 "-v, --version       Output version information and exit\n";
}

static void PrintVersion() {
    std::cout << "Citra dedicated room " << Common::g_scm_branch << " " << Common::g_scm_desc
              << " Libnetwork: " << Network::NetworkVersion - 0xFF00 << std::endl;
}

static Network::Room::BanList LoadBanList(const std::string& path) {
    std::ifstream file;
    OpenFStream(file, path, std::ios_base::in);
    if (!file || file.eof()) {
        std::cout << "Couldn't open ban list!\n\n";
        return {};
    }
    Network::Room::BanList ban_list;
    while (!file.eof()) {
        std::string line;
        std::getline(file, line);
        line.erase(std::remove(line.begin(), line.end(), '\0'), line.end());
        line = Common::StripSpaces(line);
        if (!line.empty())
            ban_list.emplace_back(line);
    }
    return ban_list;
}

static void SaveBanList(const Network::Room::BanList& ban_list, const std::string& path) {
    std::ofstream file;
    OpenFStream(file, path, std::ios_base::out);
    if (!file) {
        std::cout << "Couldn't save ban list!\n\n";
        return;
    }
    // IP ban list
    for (const auto& ip : ban_list)
        file << ip << "\n";
    file.flush();
}

/// Application entry point
int main(int argc, char** argv) {
    int option_index{};
    char* endarg;
    std::string room_name, room_description, creator, password, ban_list_file;
    u32 port{Network::DefaultRoomPort};
    bool announce{};
    static struct option long_options[]{
        {"room-name", required_argument, 0, 'n'}, {"room-description", required_argument, 0, 'd'},
        {"port", required_argument, 0, 'p'},      {"password", required_argument, 0, 'w'},
        {"creator", required_argument, 0, 'c'},   {"ban-list-file", required_argument, 0, 'b'},
        {"announce", no_argument, 0, 'a'},        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},         {0, 0, 0, 0},
    };
    while (optind < argc) {
        int arg{getopt_long(argc, argv, "n:d:p:w:c:b:a:hv", long_options, &option_index)};
        if (arg != -1) {
            switch (arg) {
            case 'n':
                room_name.assign(optarg);
                break;
            case 'd':
                room_description.assign(optarg);
                break;
            case 'p':
                port = strtoul(optarg, &endarg, 0);
                break;
            case 'w':
                password.assign(optarg);
                break;
            case 'c':
                creator.assign(optarg);
                break;
            case 'b':
                ban_list_file.assign(optarg);
                break;
            case 'a':
                announce = true;
                break;
            case 'h':
                PrintHelp(argv[0]);
                return 0;
            case 'v':
                PrintVersion();
                return 0;
            }
        }
    }
    if (room_name.empty()) {
        std::cout << "Room name is empty!\n\n";
        PrintHelp(argv[0]);
        return -1;
    }
    if (port > 65535) {
        std::cout << "port needs to be in the range 0 - 65535!\n\n";
        PrintHelp(argv[0]);
        return -1;
    }
    if (ban_list_file.empty())
        std::cout << "Ban list file not set!\nThis should get set to load and save room ban "
                     "list.\nSet with --ban-list-file <file>\n\n";
    // Load the ban list
    Network::Room::BanList ban_list;
    if (!ban_list_file.empty())
        ban_list = LoadBanList(ban_list_file);
    Network::Room room;
    room.SetErrorCallback([&announce](const Common::WebResult& result) {
        if (result.result_code != Common::WebResult::Code::Success)
            announce = false;
        std::cout << result.result_string << std::endl;
    });
    if (!room.Create(announce, room_name, room_description, creator, port, password, ban_list)) {
        std::cout << "Failed to create room!\n\n";
        return -1;
    }
    std::cout << fmt::format("Hosting a {} room\nRoom is open. Close with Q+Enter...\n\n",
                             announce ? "public" : "private");
    while (room.IsOpen()) {
        std::string in;
        std::cin >> in;
        if (in.size() > 0) {
            // Save the ban list
            if (!ban_list_file.empty())
                SaveBanList(room.GetBanList(), ban_list_file);
            room.Destroy();
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
    // Save the ban list
    if (!ban_list_file.empty())
        SaveBanList(room.GetBanList(), ban_list_file);
    room.Destroy();
    return 0;
}
