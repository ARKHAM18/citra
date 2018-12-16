// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <atomic>
#include <iomanip>
#include <mutex>
#include <random>
#include <regex>
#include <sstream>
#include <thread>
#include <httplib.h>
#include <json.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include "common/logging/log.h"
#include "common/thread_pool.h"
#include "network/message.h"
#include "network/room.h"

using ConnectionHandle = websocketpp::connection_hdl;
using Server = websocketpp::server<websocketpp::config::asio>;

namespace Network {

void to_json(nlohmann::json& json, const JsonRoom::Member& member) {
    json["nickname"] = member.nickname;
    json["program"] = member.program;
}

void from_json(const nlohmann::json& json, JsonRoom::Member& member) {
    member.nickname = json.at("nickname").get<std::string>();
    member.program = json.at("program").get<std::string>();
}

void to_json(nlohmann::json& json, const JsonRoom& room) {
    json["name"] = room.name;
    json["creator"] = room.creator;
    if (!room.description.empty())
        json["description"] = room.description;
    json["port"] = room.port;
    json["net_version"] = room.net_version;
    json["has_password"] = room.has_password;
    if (room.members.size() > 0) {
        nlohmann::json member_json = room.members;
        json["members"] = member_json;
    }
}

void from_json(const nlohmann::json& json, JsonRoom& room) {
    room.ip = json.at("ip").get<std::string>();
    room.name = json.at("name").get<std::string>();
    room.creator = json.at("creator").get<std::string>();
    try {
        room.description = json.at("description").get<std::string>();
    } catch (const nlohmann::detail::out_of_range& e) {
        room.description = "";
        LOG_DEBUG(Network, "Room '{}' doesn't contain a description", room.name);
    }
    room.port = json.at("port").get<u16>();
    room.net_version = json.at("net_version").get<u32>();
    room.has_password = json.at("has_password").get<bool>();
    try {
        room.members = json.at("members").get<std::vector<JsonRoom::Member>>();
    } catch (const nlohmann::detail::out_of_range& e) {
        LOG_DEBUG(Network, "Out of range {}", e.what());
    }
}

struct Room::RoomImpl {
    RoomImpl()
        : random_gen{std::random_device{}()}, client{std::make_unique<httplib::Client>(
                                                  "citra-valentin-api.glitch.me", 80)} {
        server.set_open_handler([](ConnectionHandle client) { LOG_CRITICAL(Log, "open"); });
        server.set_fail_handler([](ConnectionHandle client) { LOG_CRITICAL(Log, "fail"); });
        server.set_close_handler(
            [this](ConnectionHandle client) { HandleClientDisconnection(client); });
        server.set_message_handler([this](ConnectionHandle client, Server::message_ptr msg) {
            auto s{msg->get_payload()};
            std::vector<u8> data(s.size());
            std::memcpy(&data[0], &s[0], s.size());
            LOG_INFO(Log, "0x{:02X}", data[0]);
            switch (data[0]) {
            case IdJoinRequest:
                HandleJoinRequest(client, data);
                break;
            case IdSetProgram:
                HandleProgramMessage(client, data);
                break;
            case IdWifiPacket:
                HandleWifiPacket(client, data);
                break;
            case IdChatMessage:
                HandleChatMessage(client, data);
                break;
            // Moderation
            case IdModKick:
                HandleModKickMessage(client, data);
                break;
            case IdModBan:
                HandleModBanMessage(client, data);
                break;
            case IdModUnban:
                HandleModUnbanMessage(client, data);
                break;
            case IdModGetBanList:
                HandleModGetBanListMessage(client, data);
                break;
            }
        });
        server.set_http_handler([this](ConnectionHandle client) {
            auto client_ptr{server.get_con_from_hdl(client)};
            client_ptr->set_body("OK");
            client_ptr->set_status(websocketpp::http::status_code::ok);
        });
        server.init_asio();
    }

    ErrorCallback error_callback;

    std::unique_ptr<httplib::Client> client;

    std::mt19937 random_gen; ///< Random number generator. Used for GenerateMacAddress

    Server server;

    std::atomic_bool is_open{}, is_public{};
    RoomInformation room_information; ///< Information about this room.

    std::string password; ///< The password required to connect to this room.

    struct Member {
        std::string nickname; ///< The nickname of the member.
        u64 console_id;
        std::string program;     ///< The current program of the member.
        MacAddress mac_address;  ///< The assigned MAC address of the member.
        ConnectionHandle client; ///< The client handle.
    };

    using MemberList = std::vector<Member>;
    MemberList members;              ///< Information about the members of this room
    mutable std::mutex member_mutex; ///< Mutex for locking the members list

    BanList ban_list;                  ///< List of banned IP addresses
    mutable std::mutex ban_list_mutex; ///< Mutex for locking the ban list

    std::unique_ptr<std::thread>
        room_thread; ///< Thread that receives and dispatches network packets

    void ServerLoop(); ///< Thread function that will receive and dispatch messages until the room
                       ///< is destroyed.
    void StartLoop();

    /**
     * Parses and answers a room join request from a client.
     * Validates the uniqueness of the nickname and assigns the MAC address
     * that the client will use for the remainder of the client.
     */
    void HandleJoinRequest(ConnectionHandle client, const std::vector<u8>& data);

    /**
     * Parses and answers a kick request from a client.
     * Validates the permissions and that the given user exists and then kicks the member.
     */
    void HandleModKickMessage(ConnectionHandle client, const std::vector<u8>& data);

    /**
     * Parses and answers a ban request from a client.
     * Validates the permissions and bans the user by IP.
     */
    void HandleModBanMessage(ConnectionHandle client, const std::vector<u8>& data);

    /**
     * Parses and answers a unban request from a client.
     * Validates the permissions and unbans the address.
     */
    void HandleModUnbanMessage(ConnectionHandle client, const std::vector<u8>& data);

    /**
     * Parses and answers a get ban list request from a client.
     * Validates the permissions and returns the ban list.
     */
    void HandleModGetBanListMessage(ConnectionHandle client, const std::vector<u8>& data);

    /// Returns whether the nickname is valid, ie. isn't already taken by someone else
    /// in the room.
    bool IsValidNickname(const std::string& nickname) const;

    /// Returns whether the MAC address is valid, ie. isn't already taken by someone else in the
    /// room.
    bool IsValidMacAddress(const MacAddress& address) const;

    /**
     * Returns whether the console ID is valid, ie. isn't already taken by someone else in
     * the room.
     */
    bool IsValidConsoleId(u64 console_id) const;

    /// Returns whether a nickname has mod permissions.
    bool HasModPermission(ConnectionHandle client) const;

    /// Sends a IdRoomIsFull message telling the client that the room is full.
    void SendRoomIsFull(ConnectionHandle client);

    /// Sends a IdInvalidNickname message telling the client that the nickname is invalid.
    void SendInvalidNickname(ConnectionHandle client);

    /// Sends a IdMacCollision message telling the client that the MAC is invalid.
    void SendMacCollision(ConnectionHandle client);

    /**
     * Sends a IdConsoleIdCollision message telling the client that another member with the same
     * console ID exists.
     */
    void SendConsoleIdCollision(ConnectionHandle client);

    /// Sends a IdVersionMismatch message telling the client that the version is invalid.
    void SendVersionMismatch(ConnectionHandle client);

    /// Sends a IdWrongPassword message telling the client that the password is wrong.
    void SendWrongPassword(ConnectionHandle client);

    /**
     * Notifies the member that its client attempt was successful,
     * and it is now part of the room.
     */
    void SendJoinSuccess(ConnectionHandle client, MacAddress mac_address);

    /// Sends a IdHostKicked message telling the client that they have been kicked.
    void SendUserKicked(ConnectionHandle client);

    /// Sends a IdHostBanned message telling the client that they have been banned.
    void SendUserBanned(ConnectionHandle client);

    /**
     * Sends a IdModPermissionDenied message telling the client that they don't have mod
     * permission.
     */
    void SendModPermissionDenied(ConnectionHandle client);

    /// Sends a IdModNoSuchUser message telling the client that the given user couldn't be found.
    void SendModNoSuchUser(ConnectionHandle client);

    /// Sends the ban list in response to a client's request for getting ban list.
    void SendModBanListResponse(ConnectionHandle client);

    /// Notifies the members that the room is closed.
    void SendCloseMessage();

    /// Sends a system message to all the connected clients.
    void SendStatusMessage(StatusMessageTypes type, const std::string& nickname);

    /**
     * Sends the information about the room, along with the list of members
     * to every connected client in the room.
     */
    void BroadcastRoomInformation();

    /// Generates a free MAC address to assign to a new client.
    MacAddress GenerateMacAddress();

    /// Broadcasts this packet to all members except the sender.
    void HandleWifiPacket(ConnectionHandle client, const std::vector<u8>& data);

    /// Extracts a chat entry from a received message and adds it to the chat queue.
    void HandleChatMessage(ConnectionHandle client, const std::vector<u8>& data);

    /// Extracts the program from a received message and broadcasts it.
    void HandleProgramMessage(ConnectionHandle client, const std::vector<u8>& data);

    /**
     * Removes the client from the members list if it was in it and announces the change
     * to all other clients.
     */
    void HandleClientDisconnection(ConnectionHandle client);

    Common::WebResult MakeRequest(const std::string& method, const std::string& body = "");
    std::vector<JsonRoom> GetRoomList();
    void UpdateAPIInformation();

    void SetErrorCallback(ErrorCallback cb) {
        error_callback = cb;
    }
};

// RoomImpl
void Room::RoomImpl::ServerLoop() {
    server.run();
}

void Room::RoomImpl::StartLoop() {
    room_thread = std::make_unique<std::thread>(&Room::RoomImpl::ServerLoop, this);
}

void Room::RoomImpl::HandleJoinRequest(ConnectionHandle client, const std::vector<u8>& data) {
    Message message;
    message.Append(data.data(), data.size());
    message.IgnoreBytes(sizeof(u8)); // Ignore the message type
    std::string nickname;
    message >> nickname;
    u64 console_id;
    message >> console_id;
    MacAddress preferred_mac;
    message >> preferred_mac;
    u32 client_version;
    message >> client_version;
    std::string pass;
    message >> pass;
    {
        std::lock_guard lock{ban_list_mutex};
        // Check IP ban
        if (std::find(ban_list.begin(), ban_list.end(),
                      server.get_con_from_hdl(client)
                          ->get_raw_socket()
                          .remote_endpoint()
                          .address()
                          .to_string()) != ban_list.end()) {
            SendUserBanned(client);
            return;
        }
    }
    if (pass != password) {
        SendWrongPassword(client);
        return;
    }
    if (!IsValidNickname(nickname)) {
        SendInvalidNickname(client);
        return;
    }
    if (preferred_mac != BroadcastMac) {
        // Verify if the preferred MAC address is available
        if (!IsValidMacAddress(preferred_mac)) {
            SendMacCollision(client);
            return;
        }
    } else
        // Assign a MAC address of this client automatically
        preferred_mac = GenerateMacAddress();
    if (!IsValidConsoleId(console_id)) {
        SendConsoleIdCollision(client);
        return;
    }
    if (client_version != NetworkVersion) {
        SendVersionMismatch(client);
        return;
    }
    // At this point the client is ready to be added to the room.
    Member member;
    member.mac_address = preferred_mac;
    member.console_id = console_id;
    member.nickname = nickname;
    member.client = client;
    // Notify everyone that the user has joined.
    SendStatusMessage(IdMemberJoined, member.nickname);
    {
        std::lock_guard lock{member_mutex};
        members.push_back(std::move(member));
    }
    // Notify everyone that the room information has changed.
    BroadcastRoomInformation();
    SendJoinSuccess(client, preferred_mac);
}

void Room::RoomImpl::HandleModKickMessage(ConnectionHandle client, const std::vector<u8>& data) {
    if (!HasModPermission(client)) {
        SendModPermissionDenied(client);
        return;
    }
    Message message;
    message.Append(data.data(), data.size());
    message.IgnoreBytes(sizeof(u8)); // Ignore the message type
    std::string nickname;
    message >> nickname;
    {
        std::lock_guard lock{member_mutex};
        const auto target_member{
            std::find_if(members.begin(), members.end(), [&nickname](const Member& member) {
                return member.nickname == nickname;
            })};
        if (target_member == members.end()) {
            SendModNoSuchUser(client);
            return;
        }
        // Notify the kicked member
        SendUserKicked(target_member->client);
        server.close(target_member->client, websocketpp::close::status::normal, "Kicked");
        members.erase(target_member);
    }
    // Announce the change to all clients.
    SendStatusMessage(IdMemberKicked, nickname);
    BroadcastRoomInformation();
}

void Room::RoomImpl::HandleModBanMessage(ConnectionHandle client, const std::vector<u8>& data) {
    if (!HasModPermission(client)) {
        SendModPermissionDenied(client);
        return;
    }
    Message message;
    message.Append(data.data(), data.size());
    message.IgnoreBytes(sizeof(u8)); // Ignore the message type
    std::string nickname;
    message >> nickname;
    std::string ip;
    {
        std::lock_guard lock{member_mutex};
        const auto target_member{
            std::find_if(members.begin(), members.end(), [&nickname](const Member& member) {
                return member.nickname == nickname;
            })};
        if (target_member == members.end()) {
            SendModNoSuchUser(client);
            return;
        }
        // Notify the banned member
        SendUserBanned(target_member->client);
        server.close(target_member->client, websocketpp::close::status::normal, "Banned");
        nickname = target_member->nickname;
        ip = server.get_con_from_hdl(target_member->client)
                 ->get_raw_socket()
                 .remote_endpoint()
                 .address()
                 .to_string();
        members.erase(target_member);
    }
    {
        std::lock_guard lock{ban_list_mutex};
        // Ban the member's IP
        if (std::find(ban_list.begin(), ban_list.end(), ip) == ban_list.end())
            ban_list.push_back(ip);
    }
    // Announce the change to all clients.
    SendStatusMessage(IdMemberBanned, nickname);
    BroadcastRoomInformation();
}

void Room::RoomImpl::HandleModUnbanMessage(ConnectionHandle client, const std::vector<u8>& data) {
    if (!HasModPermission(client)) {
        SendModPermissionDenied(client);
        return;
    }
    Message message;
    message.Append(data.data(), data.size());
    message.IgnoreBytes(sizeof(u8)); // Ignore the message type
    std::string address;
    message >> address;
    bool unbanned{};
    {
        std::lock_guard lock{ban_list_mutex};
        auto it{std::find(ban_list.begin(), ban_list.end(), address)};
        if (it != ban_list.end()) {
            unbanned = true;
            ban_list.erase(it);
        }
    }
    if (unbanned)
        SendStatusMessage(IdAddressUnbanned, address);
    else
        SendModNoSuchUser(client);
}

void Room::RoomImpl::HandleModGetBanListMessage(ConnectionHandle client,
                                                const std::vector<u8>& data) {
    if (!HasModPermission(client)) {
        SendModPermissionDenied(client);
        return;
    }
    SendModBanListResponse(client);
}

bool Room::RoomImpl::IsValidNickname(const std::string& nickname) const {
    // A nickname is valid if it matches the regex and isn't already taken by anybody else in the
    // room.
    const std::regex nickname_regex{"^[ a-zA-Z0-9._-]{4,20}$"};
    if (!std::regex_match(nickname, nickname_regex))
        return false;
    std::lock_guard lock{member_mutex};
    return std::all_of(members.begin(), members.end(),
                       [&nickname](const Member& member) { return member.nickname != nickname; });
}

bool Room::RoomImpl::IsValidMacAddress(const MacAddress& address) const {
    // A MAC address is valid if it isn't already taken by anybody else in the room.
    std::lock_guard lock{member_mutex};
    return std::all_of(members.begin(), members.end(),
                       [&address](const Member& member) { return member.mac_address != address; });
}

bool Room::RoomImpl::IsValidConsoleId(u64 console_id) const {
    // A console ID is valid if it isn't already taken by anybody else in the room.
    std::lock_guard lock{member_mutex};
    return std::all_of(members.begin(), members.end(), [&console_id](const Member& member) {
        return member.console_id != console_id;
    });
}

bool Room::RoomImpl::HasModPermission(const ConnectionHandle client) const {
    if (room_information.creator.empty())
        return false; // This room doesn't support moderation
    std::lock_guard lock{member_mutex};
    const auto sending_member{
        std::find_if(members.begin(), members.end(), [client](const Member& member) {
            return member.client.lock() == client.lock();
        })};
    if (sending_member == members.end())
        return false;
    return sending_member->nickname == room_information.creator;
}

void Room::RoomImpl::SendInvalidNickname(ConnectionHandle client) {
    Message message;
    message << static_cast<u8>(IdInvalidNickname);
    server.send(client, message.GetData(), message.GetDataSize(),
                websocketpp::frame::opcode::binary);
}

void Room::RoomImpl::SendMacCollision(ConnectionHandle client) {
    Message message;
    message << static_cast<u8>(IdMacCollision);
    server.send(client, message.GetData(), message.GetDataSize(),
                websocketpp::frame::opcode::binary);
}

void Room::RoomImpl::SendConsoleIdCollision(ConnectionHandle client) {
    Message message;
    message << static_cast<u8>(IdConsoleIdCollision);
    server.send(client, message.GetData(), message.GetDataSize(),
                websocketpp::frame::opcode::binary);
}

void Room::RoomImpl::SendWrongPassword(ConnectionHandle client) {
    Message message;
    message << static_cast<u8>(IdWrongPassword);
    server.send(client, message.GetData(), message.GetDataSize(),
                websocketpp::frame::opcode::binary);
}

void Room::RoomImpl::SendRoomIsFull(ConnectionHandle client) {
    Message message;
    message << static_cast<u8>(IdRoomIsFull);
    server.send(client, message.GetData(), message.GetDataSize(),
                websocketpp::frame::opcode::binary);
}

void Room::RoomImpl::SendVersionMismatch(ConnectionHandle client) {
    Message message;
    message << static_cast<u8>(IdVersionMismatch);
    message << NetworkVersion;
    server.send(client, message.GetData(), message.GetDataSize(),
                websocketpp::frame::opcode::binary);
}

void Room::RoomImpl::SendJoinSuccess(ConnectionHandle client, MacAddress mac_address) {
    Message message;
    message << static_cast<u8>(IdJoinSuccess);
    message << mac_address;
    server.send(client, message.GetData(), message.GetDataSize(),
                websocketpp::frame::opcode::binary);
}

void Room::RoomImpl::SendUserKicked(ConnectionHandle client) {
    Message message;
    message << static_cast<u8>(IdHostKicked);
    server.send(client, message.GetData(), message.GetDataSize(),
                websocketpp::frame::opcode::binary);
}

void Room::RoomImpl::SendUserBanned(ConnectionHandle client) {
    Message message;
    message << static_cast<u8>(IdHostBanned);
    server.send(client, message.GetData(), message.GetDataSize(),
                websocketpp::frame::opcode::binary);
}

void Room::RoomImpl::SendModPermissionDenied(ConnectionHandle client) {
    Message message;
    message << static_cast<u8>(IdModPermissionDenied);
    server.send(client, message.GetData(), message.GetDataSize(),
                websocketpp::frame::opcode::binary);
}

void Room::RoomImpl::SendModNoSuchUser(ConnectionHandle client) {
    Message message;
    message << static_cast<u8>(IdModNoSuchUser);
    server.send(client, message.GetData(), message.GetDataSize(),
                websocketpp::frame::opcode::binary);
}

void Room::RoomImpl::SendModBanListResponse(ConnectionHandle client) {
    Message message;
    message << static_cast<u8>(IdModBanListResponse);
    {
        std::lock_guard lock{ban_list_mutex};
        message << ban_list;
    }
    server.send(client, message.GetData(), message.GetDataSize(),
                websocketpp::frame::opcode::binary);
}

void Room::RoomImpl::SendCloseMessage() {
    Message message;
    message << static_cast<u8>(IdCloseRoom);
    std::lock_guard lock{member_mutex};
    for (const auto& member : members) {
        server.send(member.client, message.GetData(), message.GetDataSize(),
                    websocketpp::frame::opcode::binary);
        server.close(member.client, websocketpp::close::status::normal, "Room closing");
    }
}

void Room::RoomImpl::SendStatusMessage(StatusMessageTypes type, const std::string& nickname) {
    Message message;
    message << static_cast<u8>(IdStatusMessage);
    message << static_cast<u8>(type);
    message << nickname;
    std::lock_guard lock{member_mutex};
    for (const auto& member : members)
        server.send(member.client, message.GetData(), message.GetDataSize(),
                    websocketpp::frame::opcode::binary);
}

void Room::RoomImpl::BroadcastRoomInformation() {
    Message message;
    message << static_cast<u8>(IdRoomInformation);
    message << room_information.name;
    message << room_information.description;
    message << room_information.port;
    message << room_information.creator;
    message << static_cast<u32>(members.size());
    std::lock_guard lock{member_mutex};
    for (const auto& member : members) {
        message << member.nickname;
        message << member.mac_address;
        message << member.program;
    }
    for (const auto& member : members)
        server.send(member.client, message.GetData(), message.GetDataSize(),
                    websocketpp::frame::opcode::binary);
    if (is_public.load(std::memory_order_relaxed)) {
        // Update API information
        auto& thread_pool{Common::ThreadPool::GetPool()};
        thread_pool.Push([this] { UpdateAPIInformation(); });
    }
}

MacAddress Room::RoomImpl::GenerateMacAddress() {
    // The first three bytes of each MAC address will be the NintendoOUI
    MacAddress result_mac{NintendoOUI};
    std::uniform_int_distribution<> dis{0x00, 0xFF}; // Random byte between 0 and 0xFF
    do {
        for (std::size_t i{3}; i < result_mac.size(); ++i)
            result_mac[i] = dis(random_gen);
    } while (!IsValidMacAddress(result_mac));
    return result_mac;
}

void Room::RoomImpl::HandleWifiPacket(ConnectionHandle client, const std::vector<u8>& data) {
    Message message;
    message.Append(data.data(), data.size());
    message.IgnoreBytes(sizeof(u8));         // Message type
    message.IgnoreBytes(sizeof(u8));         // WifiPacket Type
    message.IgnoreBytes(sizeof(u8));         // WifiPacket Channel
    message.IgnoreBytes(sizeof(MacAddress)); // WifiPacket Transmitter Address
    MacAddress destination_address;
    message >> destination_address;
    if (destination_address == BroadcastMac) { // Send the data to everyone except the sender
        std::lock_guard lock{member_mutex};
        for (const auto& member : members)
            if (member.client.lock() != client.lock())
                server.send(member.client, data.data(), data.size(),
                            websocketpp::frame::opcode::binary);
    } else { // Send the data only to the destination client
        std::lock_guard lock{member_mutex};
        auto member{std::find_if(members.begin(), members.end(),
                                 [destination_address](const Member& member) -> bool {
                                     return member.mac_address == destination_address;
                                 })};
        if (member != members.end())
            server.send(member->client, data.data(), data.size(),
                        websocketpp::frame::opcode::binary);
        else
            LOG_ERROR(Network,
                      "Attempting to send to unknown MAC address: "
                      "{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
                      destination_address[0], destination_address[1], destination_address[2],
                      destination_address[3], destination_address[4], destination_address[5]);
    }
}

void Room::RoomImpl::HandleChatMessage(ConnectionHandle client, const std::vector<u8>& data) {
    Message in_message;
    in_message.Append(data.data(), data.size());
    in_message.IgnoreBytes(sizeof(u8)); // Ignore the message type
    std::string message;
    in_message >> message;
    auto CompareNetworkAddress{
        [&client](const Member& member) -> bool { return member.client.lock() == client.lock(); }};
    std::lock_guard lock{member_mutex};
    const auto sending_member{std::find_if(members.begin(), members.end(), CompareNetworkAddress)};
    if (sending_member == members.end())
        return; // Received a chat message from a unknown sender
    // Limit the size of chat messages to MaxMessageSize
    message.resize(MaxMessageSize);
    Message out_message;
    out_message << static_cast<u8>(IdChatMessage);
    out_message << sending_member->nickname;
    out_message << message;
    for (const auto& member : members)
        if (member.client.lock() != client.lock())
            server.send(member.client, out_message.GetData(), out_message.GetDataSize(),
                        websocketpp::frame::opcode::binary);
}

void Room::RoomImpl::HandleProgramMessage(ConnectionHandle client, const std::vector<u8>& data) {
    Message message;
    message.Append(data.data(), data.size());
    message.IgnoreBytes(sizeof(u8)); // Ignore the message type
    std::string program;
    message >> program;
    {
        std::lock_guard lock{member_mutex};
        auto member{
            std::find_if(members.begin(), members.end(), [&client](const Member& member) -> bool {
                return member.client.lock() == client.lock();
            })};
        if (member != members.end())
            member->program = program;
    }
    BroadcastRoomInformation();
}

void Room::RoomImpl::HandleClientDisconnection(ConnectionHandle client) {
    // Remove the client from the members list.
    std::string nickname;
    {
        std::lock_guard lock{member_mutex};
        auto member{std::find_if(members.begin(), members.end(), [&client](const Member& member) {
            return member.client.lock() == client.lock();
        })};
        if (member != members.end()) {
            nickname = member->nickname;
            members.erase(member);
        }
    }
    // Announce the change to all clients.
    SendStatusMessage(IdMemberLeft, nickname);
    BroadcastRoomInformation();
}

Common::WebResult Room::RoomImpl::MakeRequest(const std::string& method, const std::string& body) {
    auto response{method == "GET" ? client->Get("/lobby")
                                  : client->Post("/lobby", body, "application/json")};
    if (!response) {
        LOG_ERROR(Network, "Request returned null");
        return Common::WebResult{Common::WebResult::Code::LibError, "Null response"};
    }
    if (response->status >= 400) {
        LOG_ERROR(Network, "Request returned error status code: {}", response->status);
        return Common::WebResult{Common::WebResult::Code::HttpError,
                                 std::to_string(response->status)};
    }
    auto content_type{response->headers.find("Content-Type")};
    if (content_type == response->headers.end()) {
        LOG_ERROR(Network, "Request returned no content");
        return Common::WebResult{Common::WebResult::Code::WrongContent, "No content"};
    }
    if (content_type->second.find("application/json") == std::string::npos &&
        content_type->second.find("text/html") == std::string::npos &&
        content_type->second.find("text/plain") == std::string::npos) {
        LOG_ERROR(Network, "Request returned wrong content: {}", content_type->second);
        return Common::WebResult{Common::WebResult::Code::WrongContent, "Wrong content"};
    }
    if (response->body == "You need to open the TCP ports to create a public room.")
        return Common::WebResult{Common::WebResult::Code::HttpError, response->body};
    return Common::WebResult{Common::WebResult::Code::Success, "", response->body};
}

std::vector<JsonRoom> Room::RoomImpl::GetRoomList() {
    auto reply{MakeRequest("GET").returned_data};
    if (reply.empty())
        return {};
    return nlohmann::json::parse(reply).get<std::vector<JsonRoom>>();
}

void Room::RoomImpl::UpdateAPIInformation() {
    std::lock_guard lock{member_mutex};
    JsonRoom room;
    room.name = room_information.name;
    room.creator = room_information.creator;
    room.description = room_information.description;
    room.port = room_information.port;
    room.net_version = Network::NetworkVersion;
    room.has_password = !password.empty();
    for (const auto& member : members)
        room.members.emplace_back(
            JsonRoom::Member{member.nickname, member.program, member.mac_address});
    nlohmann::json json = room;
    auto result{MakeRequest("POST", json.dump())};
    if (result.result_code != Common::WebResult::Code::Success &&
        is_public.load(std::memory_order_relaxed) && error_callback)
        return error_callback(result);
}

// Room
Room::Room() : room_impl{std::make_unique<RoomImpl>()} {}
Room::~Room() = default;

bool Room::Create(bool is_public, const std::string& name, const std::string& description,
                  const std::string& creator, u16 port, const std::string& password,
                  const Room::BanList& ban_list) {
    room_impl->is_open.store(true, std::memory_order_relaxed);
    room_impl->room_information.name = name;
    room_impl->room_information.creator = creator;
    room_impl->room_information.description = description;
    room_impl->room_information.port = port;
    room_impl->password = password;
    room_impl->ban_list = ban_list;
    room_impl->is_public.store(is_public, std::memory_order_relaxed);
    room_impl->server.listen(port);
    room_impl->server.start_accept();
    room_impl->StartLoop();
    if (is_public)
        room_impl->UpdateAPIInformation();
    return true;
}

bool Room::IsOpen() const {
    return room_impl->is_open.load(std::memory_order_relaxed);
}

const RoomInformation& Room::GetRoomInformation() const {
    return room_impl->room_information;
}

Room::BanList Room::GetBanList() const {
    std::lock_guard lock{room_impl->ban_list_mutex};
    return room_impl->ban_list;
}

std::vector<Room::Member> Room::GetRoomMemberList() const {
    std::vector<Room::Member> member_list;
    std::lock_guard lock{room_impl->member_mutex};
    for (const auto& member_impl : room_impl->members) {
        Member member;
        member.nickname = member_impl.nickname;
        member.mac_address = member_impl.mac_address;
        member.program = member_impl.program;
        member_list.push_back(member);
    }
    return member_list;
}

bool Room::HasPassword() const {
    return !room_impl->password.empty();
}

void Room::Destroy() {
    room_impl->is_open.store(false, std::memory_order_relaxed);
    // Close the connection to all members
    room_impl->SendCloseMessage();
    room_impl->server.stop_listening();
    room_impl->server.stop();
    room_impl->room_thread->join();
    room_impl->room_thread.reset();
    if (room_impl->is_public.load(std::memory_order_relaxed)) {
        nlohmann::json json;
        json["delete"] = room_impl->room_information.port;
        room_impl->MakeRequest("POST", json.dump());
    }
    room_impl->room_information = {};
    {
        std::lock_guard lock{room_impl->member_mutex};
        room_impl->members.clear();
    }
}

std::vector<JsonRoom> Room::GetRoomList() {
    return room_impl->GetRoomList();
}

void Room::SetErrorCallback(ErrorCallback cb) {
    return room_impl->SetErrorCallback(cb);
}

void Room::StopAnnouncing() {
    room_impl->is_public.store(false, std::memory_order_relaxed);
}

bool Room::IsPublic() const {
    return room_impl->is_public.load(std::memory_order_relaxed);
}

} // namespace Network
