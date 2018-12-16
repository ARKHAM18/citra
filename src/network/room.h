// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "common/common_types.h"
#include "common/web_result.h"

namespace Network {

constexpr u32 NetworkVersion{0xFF04}; ///< The network version
constexpr u16 DefaultRoomPort{24872};
constexpr u32 MaxMessageSize{500};
constexpr std::size_t NumChannels{1}; // Number of channels used for the connection

struct RoomInformation {
    std::string name;        ///< Name of the room
    std::string description; ///< Room description
    u16 port;                ///< The port of this room
    std::string creator;     ///< The creator of this room
};

struct JsonRoom {
    struct Member {
        std::string nickname, program;
        MacAddress mac_address;
    };
    std::string name, creator, description, ip;
    u16 port;
    u32 net_version;
    bool has_password;
    std::vector<Member> members;
};

// The different types of messages that can be sent. The first byte of each packet defines the type
enum RoomMessageTypes : u8 {
    IdJoinRequest = 1,
    IdJoinSuccess,
    IdRoomInformation,
    IdSetProgram,
    IdWifiPacket,
    IdChatMessage,
    IdInvalidNickname,
    IdMacCollision,
    IdVersionMismatch,
    IdWrongPassword,
    IdCloseRoom,
    IdRoomIsFull,
    IdStatusMessage,
    IdConsoleIdCollision,
    IdHostKicked,
    IdHostBanned,
    // Moderation requests
    IdModKick,
    IdModBan,
    IdModUnban,
    IdModGetBanList,
    // Moderation responses
    IdModBanListResponse,
    IdModPermissionDenied,
    IdModNoSuchUser,
};

/// Types of system status messages
enum StatusMessageTypes : u8 {
    IdMemberJoined = 1, ///< A member joined
    IdMemberLeft,       ///< A member left
    IdMemberKicked,     ///< A member was kicked from the room
    IdMemberBanned,     ///< A member was banned from the room
    IdAddressUnbanned,  ///< A IP address was unbanned from the room
};

using ErrorCallback = std::function<void(const Common::WebResult&)>;

/// This is what a server [person creating a server] would use.
class Room final {
public:
    struct Member {
        std::string nickname;   ///< The nickname of the member.
        std::string program;    ///< The current program of the member.
        MacAddress mac_address; ///< The assigned MAC address of the member.
    };

    using BanList = std::vector<std::string>;

    Room();
    ~Room();

    /// Return whether the room is open.
    bool IsOpen() const;

    /// Gets the room information of the room.
    const RoomInformation& GetRoomInformation() const;

    /// Gets a list of the mbmers connected to the room.
    std::vector<Member> GetRoomMemberList() const;

    /// Checks if the room is password protected
    bool HasPassword() const;

    /// Creates the socket for this room
    bool Create(bool is_public, const std::string& name, const std::string& description,
                const std::string& creator, u16 port = DefaultRoomPort,
                const std::string& password = "", const BanList& ban_list = {});

    /// Gets the banned IPs of the room.
    BanList GetBanList() const;

    /// Destroys the socket
    void Destroy();

    // Gets the room list
    std::vector<JsonRoom> GetRoomList();

    /// Sets a function to call when a error happens in 'MakeRequest'
    void SetErrorCallback(ErrorCallback cb);

    /// Stops announcing the room
    void StopAnnouncing();

    bool IsPublic() const;

private:
    Common::WebResult MakeRequest(const std::string& method, const std::string& body = "");

    struct RoomImpl;
    std::unique_ptr<RoomImpl> room_impl;
};

} // namespace Network
