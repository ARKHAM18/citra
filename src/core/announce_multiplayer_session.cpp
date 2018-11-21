// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <vector>
#include "announce_multiplayer_session.h"
#include "common/announce_multiplayer_room.h"
#include "common/assert.h"
#include "core/core.h"
#include "core/settings.h"
#include "network/room.h"

#ifdef ENABLE_WEB_SERVICE
#include "web_service/announce_room_json.h"
#endif

namespace Core {

// Time between room is announced to web services
constexpr std::chrono::seconds announce_time_interval{15};

AnnounceMultiplayerSession::AnnounceMultiplayerSession(Network::Room& room) : room{room} {
#ifdef ENABLE_WEB_SERVICE
    backend = std::make_unique<WebService::RoomJson>(Settings::values.web_api_url,
                                                     Settings::values.citra_username,
                                                     Settings::values.citra_token);
#else
    backend = std::make_unique<AnnounceMultiplayerRoom::NullBackend>();
#endif
}

void AnnounceMultiplayerSession::Start() {
    if (announce_multiplayer_thread)
        Stop();
    shutdown_event.Reset();
    announce_multiplayer_thread =
        std::make_unique<std::thread>(&AnnounceMultiplayerSession::AnnounceMultiplayerLoop, this);
}

void AnnounceMultiplayerSession::Stop() {
    if (announce_multiplayer_thread) {
        shutdown_event.Set();
        announce_multiplayer_thread->join();
        announce_multiplayer_thread.reset();
        backend->Delete();
    }
}

AnnounceMultiplayerSession::CallbackHandle AnnounceMultiplayerSession::BindErrorCallback(
    std::function<void(const Common::WebResult&)> function) {
    std::lock_guard lock{callback_mutex};
    auto handle{std::make_shared<std::function<void(const Common::WebResult&)>>(function)};
    error_callbacks.insert(handle);
    return handle;
}

void AnnounceMultiplayerSession::UnbindErrorCallback(CallbackHandle handle) {
    std::lock_guard lock{callback_mutex};
    error_callbacks.erase(handle);
}

AnnounceMultiplayerSession::~AnnounceMultiplayerSession() {
    Stop();
}

void AnnounceMultiplayerSession::AnnounceMultiplayerLoop() {
    auto update_time{std::chrono::steady_clock::now()};
    std::future<Common::WebResult> future;
    while (!shutdown_event.WaitUntil(update_time)) {
        update_time += announce_time_interval;
        if (!room.IsOpen())
            break;
        auto room_information{room.GetRoomInformation()};
        auto member_list{room.GetRoomMemberList()};
        backend->SetRoomInformation(
            room_information.uid, room_information.name, room_information.port,
            room_information.member_slots, Network::network_version, room.HasPassword(),
            room_information.preferred_program, room_information.preferred_program_id);
        backend->ClearMembers();
        for (const auto& member : member_list)
            backend->AddMember(member.nickname, member.mac_address, member.program_info.id,
                               member.program_info.name);
        auto result{backend->Announce()};
        if (result.result_code != Common::WebResult::Code::Success) {
            std::lock_guard lock{callback_mutex};
            for (auto callback : error_callbacks)
                (*callback)(result);
        }
    }
}

AnnounceMultiplayerRoom::RoomList AnnounceMultiplayerSession::GetRoomList() {
    return backend->GetRoomList();
}

} // namespace Core
