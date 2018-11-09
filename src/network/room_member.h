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

/// Information about the received WiFi packets.
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
    MACAddress transmitter_address; ///< MAC address of the transmitter.
    MACAddress destination_address; ///< MAC address of the receiver.
    u8 channel;                     ///< WiFi channel where this frame was transmitted.
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
        Idle,          ///< Default state
        Error,         ///< Some error [permissions to network device missing or something]
        Joining,       ///< The client is attempting to join a room.
        Joined, ///< The client is connected to the room and is ready to send/receive packets.
        LostConnection, ///< Connection closed

        // Reasons why connection was rejected
        NameCollision,   ///< Somebody is already using this name
        MacCollision,    ///< Somebody is already using that mac-address
        WrongVersion,    ///< The room version is not the same as for this RoomMember
        WrongPassword,   ///< The password doesn't match the one from the Room
        CouldNotConnect, ///< The room is not responding to a connection attempt
        RoomIsFull       ///< Room is already at the maximum number of members
    };

    struct MemberInformation {
        std::string nickname;   ///< Nickname of the member.
        std::string program;    ///< Program that the member is running. Empty if the member isn't
                                ///< running a program.
        MACAddress mac_address; ///< MAC address associated with this member.
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
    const MACAddress& GetMacAddress() const;

    /// Returns information about the room we're currently connected to.
    RoomInformation GetRoomInformation() const;

    /// Returns whether we're connected to a server or not.
    bool IsConnected() const;

    /**
     * Attempts to join a room at the specified address and port, using the specified nickname.
     * This may fail if the username is already taken.
     */
    void Join(const std::string& nickname, const char* server_addr = "127.0.0.1",
              const u16 server_port = DefaultRoomPort,
              const MACAddress& preferred_mac = BroadcastMac, const std::string& password = "");

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
    void SendProgram(const std::string& program);

    /**
     * Binds a function to an event that will be triggered every time the State of the member
     * changed. The function wil be called every time the event is triggered. The callback function
     * must not bind or unbind a function. Doing so will cause a deadlock
     * @param callback The function to call
     * @return A handle used for removing the function from the registered list
     */
    CallbackHandle<State> BindOnStateChanged(std::function<void(const State&)> callback);

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
    CallbackHandle<ChatEntry> BindOnChatMessageRecieved(
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
    case RoomMember::State::Error:
        return "Error";
    case RoomMember::State::Joining:
        return "Joining";
    case RoomMember::State::Joined:
        return "Joined";
    case RoomMember::State::LostConnection:
        return "LostConnection";
    case RoomMember::State::NameCollision:
        return "NameCollision";
    case RoomMember::State::MacCollision:
        return "MacCollision";
    case RoomMember::State::WrongVersion:
        return "WrongVersion";
    case RoomMember::State::WrongPassword:
        return "WrongPassword";
    case RoomMember::State::CouldNotConnect:
        return "CouldNotConnect";
    }
    return "Unknown";
}

} // namespace Network
