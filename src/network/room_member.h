// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "common/common_types.h"
#include "network/room.h"

namespace Network {

/// Information about the received Wifi packets.
/// Acts as our own 802.11 header.
struct WifiPacket {
    enum class PacketType : u8 {
        Beacon,
        Data,
        Authentication,
        AssociationResponse,
        Deauthentication,
        NodeMap
    };
    PacketType type;      ///< The type of 802.11 frame.
    std::vector<u8> data; ///< Raw 802.11 frame data, starting at the management frame header
                          /// for management frames.
    MacAddress transmitter_address; ///< MAC address of the transmitter.
    MacAddress destination_address; ///< MAC address of the receiver.
    u8 channel;                     ///< Wifi channel where this frame was transmitted.
};

/// Represents a chat message.
struct ChatEntry {
    std::string nickname; ///< Nickname of the client who sent this message.
    std::string message;  ///< Body of the message.
};

/// Represents a system status message.
struct StatusMessageEntry {
    StatusMessageTypes type; ///< Type of the message
    /// Subject of the message. i.e. the user who is joining/leaving/being banned, etc.
    std::string nickname;
};

/**
 * This is what a client [person joining a server] would use.
 * It also has to be used if you host a room yourself (You'd create both, a Room and a
 * RoomMember for yourself)
 */
class RoomMember final {
public:
    enum class State : u8 {
        Uninitialized, ///< Not initialized
        Idle,          ///< Default state (i.e. not connected)
        Joining,       ///< The client is attempting to join a room.
        Joined, ///< The client is connected to the room and is ready to send/receive packets.
    };

    enum class Error : u8 {
        // Reasons why connection was closed
        LostConnection, ///< Connection closed
        HostKicked,     ///< Kicked by the host

        // Reasons why connection was rejected
        UnknownError,       ///< Some error [permissions to network device missing or something]
        InvalidNickname,    ///< Somebody is already using this nickname
        MacCollision,       ///< Somebody is already using that MAC address
        ConsoleIdCollision, ///< Somebody in the room has the same console ID
        WrongVersion,       ///< The room version isn't the same as for this RoomMember
        WrongPassword,      ///< The password doesn't match the one from the Room
        CouldNotConnect,    ///< The room isn't responding to a connection attempt
        RoomIsFull,         ///< Room is already at the maximum number of members
        HostBanned,         ///< The user is banned by the host

        // Reasons why moderation request failed
        PermissionDenied, ///< The user doesn't have mod permissions
        NoSuchUser,       ///< The nickname the user attempts to kick/ban doesn't exist
    };

    struct MemberInformation {
        std::string nickname;   ///< Nickname of the member.
        std::string program;    ///< Program that the member is running. Empty if the member isn't
                                ///< running a program.
        MacAddress mac_address; ///< MAC address associated with this member.
    };

    using MemberList = std::vector<MemberInformation>;

    // The handle for the callback functions
    template <typename T>
    using CallbackHandle = std::shared_ptr<std::function<void(const T&)>>;

    /**
     * Unbinds a callback function from the events.
     * @param handle The connection handle to disconnect
     */
    template <typename T>
    void Unbind(CallbackHandle<T> handle);

    RoomMember();
    ~RoomMember();

    /// Returns the status of our connection to the room.
    State GetState() const;

    /// Returns information about the members in the room we're currently connected to.
    const MemberList& GetMemberInformation() const;

    /// Returns the nickname of the RoomMember.
    const std::string& GetNickname() const;

    /// Returns the MAC address of the RoomMember.
    const MacAddress& GetMacAddress() const;

    /// Returns information about the room we're currently connected to.
    RoomInformation GetRoomInformation() const;

    /// Returns whether we're connected to a server or not.
    bool IsConnected() const;

    /**
     * Attempts to join a room at the specified address and port, using the specified nickname and
     * preferred MAC address. The console ID is passed in to check console ID conflicts. This may
     * fail if the nickname or console ID is already taken.
     */
    void Join(const std::string& nickname, u64 console_id, const char* server_addr = "127.0.0.1",
              const u16 server_port = DefaultRoomPort,
              const MacAddress& preferred_mac = BroadcastMac, const std::string& password = "");

    /**
     * Sends a WiFi packet to the room.
     * @param packet The WiFi packet to send.
     */
    void SendWifiPacket(const WifiPacket& packet);

    /**
     * Sends a chat message to the room.
     * @param message The contents of the message.
     */
    void SendChatMessage(const std::string& message);

    /**
     * Sends the current program to the room.
     * @param program The program name.
     */
    void SetProgram(const std::string& program);

    /**
     * Sends a moderation request to the room.
     * @param type Moderation request type.
     * @param nickname The subject of the request. (i.e. the user you want to kick/ban)
     */
    void SendModerationRequest(RoomMessageTypes type, const std::string& nickname);

    /**
     * Attempts to retrieve ban list from the room.
     * If success, the ban list callback would be called. Otherwise an error would be emitted.
     */
    void RequestBanList();

    /**
     * Binds a function to an event that will be triggered every time the State of the member
     * changed. The function wil be called every time the event is triggered. The callback function
     * must not bind or unbind a function. Doing so will cause a deadlock
     * @param callback The function to call
     * @return A handle used for removing the function from the registered list
     */
    CallbackHandle<State> BindOnStateChanged(std::function<void(const State&)> callback);

    /**
     * Binds a function to an event that will be triggered every time an error happened. The
     * function wil be called every time the event is triggered. The callback function must not bind
     * or unbind a function. Doing so will cause a deadlock
     * @param callback The function to call
     * @return A handle used for removing the function from the registered list
     */
    CallbackHandle<Error> BindOnError(std::function<void(const Error&)> callback);

    /**
     * Binds a function to an event that will be triggered every time a WifiPacket is received.
     * The function wil be called everytime the event is triggered.
     * The callback function must not bind or unbind a function. Doing so will cause a deadlock
     * @param callback The function to call
     * @return A handle used for removing the function from the registered list
     */
    CallbackHandle<WifiPacket> BindOnWifiPacketReceived(
        std::function<void(const WifiPacket&)> callback);

    /**
     * Binds a function to an event that will be triggered every time the RoomInformation changes.
     * The function wil be called every time the event is triggered.
     * The callback function must not bind or unbind a function. Doing so will cause a deadlock
     * @param callback The function to call
     * @return A handle used for removing the function from the registered list
     */
    CallbackHandle<RoomInformation> BindOnRoomInformationChanged(
        std::function<void(const RoomInformation&)> callback);

    /**
     * Binds a function to an event that will be triggered every time a ChatMessage is received.
     * The function wil be called every time the event is triggered.
     * The callback function must not bind or unbind a function. Doing so will cause a deadlock
     * @param callback The function to call
     * @return A handle used for removing the function from the registered list
     */
    CallbackHandle<ChatEntry> BindOnChatMessageReceived(
        std::function<void(const ChatEntry&)> callback);

    /**
     * Binds a function to an event that will be triggered every time a StatusMessage is
     * received. The function will be called every time the event is triggered. The callback
     * function must not bind or unbind a function. Doing so will cause a deadlock
     * @param callback The function to call
     * @return A handle used for removing the function from the registered list
     */
    CallbackHandle<StatusMessageEntry> BindOnStatusMessageReceived(
        std::function<void(const StatusMessageEntry&)> callback);

    /**
     * Binds a function to an event that will be triggered every time a requested ban list
     * received. The function will be called every time the event is triggered. The callback
     * function must not bind or unbind a function. Doing so will cause a deadlock
     * @param callback The function to call
     * @return A handle used for removing the function from the registered list
     */
    CallbackHandle<Room::BanList> BindOnBanListReceived(
        std::function<void(const Room::BanList&)> callback);

    /// Leaves the current room.
    void Leave();

private:
    struct RoomMemberImpl;
    std::unique_ptr<RoomMemberImpl> room_member_impl;
};

static const char* GetStateStr(const RoomMember::State& s) {
    switch (s) {
    case RoomMember::State::Idle:
        return "Idle";
    case RoomMember::State::Joining:
        return "Joining";
    case RoomMember::State::Joined:
        return "Joined";
    }
    return "Unknown";
}

static const char* GetErrorStr(const RoomMember::Error& e) {
    switch (e) {
    case RoomMember::Error::LostConnection:
        return "LostConnection";
    case RoomMember::Error::HostKicked:
        return "HostKicked";
    case RoomMember::Error::UnknownError:
        return "UnknownError";
    case RoomMember::Error::InvalidNickname:
        return "InvalidNickname";
    case RoomMember::Error::MacCollision:
        return "MacCollision";
    case RoomMember::Error::ConsoleIdCollision:
        return "ConsoleIdCollision";
    case RoomMember::Error::WrongVersion:
        return "WrongVersion";
    case RoomMember::Error::WrongPassword:
        return "WrongPassword";
    case RoomMember::Error::CouldNotConnect:
        return "CouldNotConnect";
    case RoomMember::Error::RoomIsFull:
        return "RoomIsFull";
    case RoomMember::Error::HostBanned:
        return "HostBanned";
    case RoomMember::Error::PermissionDenied:
        return "PermissionDenied";
    case RoomMember::Error::NoSuchUser:
        return "NoSuchUser";
    }
    return "Unknown";
}

} // namespace Network
