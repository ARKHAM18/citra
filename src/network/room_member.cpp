// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <atomic>
#include <list>
#include <mutex>
#include <set>
#include <thread>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include "common/assert.h"
#include "network/message.h"
#include "network/room_member.h"

using Client = websocketpp::client<websocketpp::config::asio_client>;
using ConnectionHandle = websocketpp::connection_hdl;

namespace Network {

struct RoomMember::RoomMemberImpl {
    Client client;
    Client::connection_ptr connection;

    /// Information about the clients connected to the same room as us.
    MemberList member_information;

    /// Information about the room we're connected to.
    RoomInformation room_information;

    /// The current program
    std::string current_program;

    std::atomic<State> state{State::Idle}; ///< Current state of the RoomMember.

    void SetState(const State new_state);
    void SetError(const Error new_error);
    bool IsConnected() const;
    void HandleMessage(ConnectionHandle, Client::message_ptr msg);

    std::string nickname;   ///< The nickname of this member.
    MacAddress mac_address; ///< The mac_address of this member.

    std::mutex network_mutex; ///< Mutex that controls access to the client variable.
    std::unique_ptr<std::thread>
        loop_thread; ///< Thread that receives and dispatches network packets

    template <typename T>
    using CallbackSet = std::set<CallbackHandle<T>>;

    std::mutex callback_mutex; ///< The mutex used for handling callbacks

    class Callbacks {
    public:
        template <typename T>
        CallbackSet<T>& Get();

    private:
        CallbackSet<WifiPacket> callback_set_wifi_packet;
        CallbackSet<ChatEntry> callback_set_chat_message;
        CallbackSet<StatusMessageEntry> callback_set_status_message;
        CallbackSet<RoomInformation> callback_set_room_information;
        CallbackSet<State> callback_set_state;
        CallbackSet<Error> callback_set_error;
        CallbackSet<Room::BanList> callback_set_ban_list;
    };

    Callbacks callbacks; ///< All CallbackSets to all messages

    void MemberLoop();
    void StartLoop();

    /**
     * Sends data to the room. It will be send on channel 0 with flag RELIABLE
     * @param message The data to send
     */
    void Send(Message&& message);

    /**
     * Sends a request to the server, asking for permission to join a room with the specified
     * nickname and preferred MAC address.
     * @param nickname The desired nickname.
     * @param console_id The console ID.
     * @param preferred_mac The preferred MAC address to use in the room, the BroadcastMac tells the
     * server to assign one for us.
     * @param password The password for the room
     */
    void SendJoinRequest(const std::string& nickname, u64 console_id,
                         const MacAddress& preferred_mac = BroadcastMac,
                         const std::string& password = "");

    /// Extracts a MAC address from a received message and sends the current program.
    void HandleJoinMessage(const std::vector<u8>& data);

    /// Extracts RoomInformation and MemberInformation from a received message.
    void HandleRoomInformationMessage(const std::vector<u8>& data);

    /// Extracts a WifiPacket from a received message.
    void HandleWifiPacket(const std::vector<u8>& data);

    /// Extracts a chat entry from a received message and adds it to the chat queue.
    void HandleChatMessage(const std::vector<u8>& data);

    /**
     * Extracts a system message entry from a received message and adds it to the system message
     * queue.
     */
    void HandleStatusMessageMessage(const std::vector<u8>& data);

    /// Extracts a ban list request response from a received message.
    void HandleModBanListResponseMessage(const std::vector<u8>& data);

    template <typename T>
    void Invoke(const T& data);

    template <typename T>
    CallbackHandle<T> Bind(std::function<void(const T&)> callback);
};

// RoomMemberImpl
void RoomMember::RoomMemberImpl::SetState(const State new_state) {
    if (state != new_state) {
        state = new_state;
        Invoke<State>(state);
    }
}

void RoomMember::RoomMemberImpl::SetError(const Error new_error) {
    Invoke<Error>(new_error);
}

bool RoomMember::RoomMemberImpl::IsConnected() const {
    return state == State::Joining || state == State::Joined;
}

void RoomMember::RoomMemberImpl::HandleMessage(ConnectionHandle, Client::message_ptr msg) {
    auto s{msg->get_payload()};
    std::vector<u8> data(s.size());
    std::memcpy(&data[0], &s[0], s.size());
    switch (data[0]) {
    case IdWifiPacket:
        HandleWifiPacket(data);
        break;
    case IdChatMessage:
        HandleChatMessage(data);
        break;
    case IdStatusMessage:
        HandleStatusMessageMessage(data);
        break;
    case IdRoomInformation:
        HandleRoomInformationMessage(data);
        break;
    case IdJoinSuccess:
        // The join request was successful, we're now in the room.
        // If we joined successfully, there must be at least one client in the room: us.
        ASSERT_MSG(member_information.size() > 0, "We have not yet received member information.");
        HandleJoinMessage(data);
        SetState(State::Joined);
        break;
    case IdModBanListResponse:
        HandleModBanListResponseMessage(data);
        break;
    case IdRoomIsFull:
        SetState(State::Idle);
        SetError(Error::RoomIsFull);
        break;
    case IdInvalidNickname:
        SetState(State::Idle);
        SetError(Error::InvalidNickname);
        break;
    case IdMacCollision:
        SetState(State::Idle);
        SetError(Error::MacCollision);
        break;
    case IdConsoleIdCollision:
        SetState(State::Idle);
        SetError(Error::ConsoleIdCollision);
        break;
    case IdVersionMismatch:
        SetState(State::Idle);
        SetError(Error::WrongVersion);
        break;
    case IdWrongPassword:
        SetState(State::Idle);
        SetError(Error::WrongPassword);
        break;
    case IdCloseRoom:
        SetState(State::Idle);
        SetError(Error::LostConnection);
        break;
    case IdHostKicked:
        SetState(State::Idle);
        SetError(Error::HostKicked);
        break;
    case IdHostBanned:
        SetState(State::Idle);
        SetError(Error::HostBanned);
        break;
    case IdModPermissionDenied:
        SetError(Error::PermissionDenied);
        break;
    case IdModNoSuchUser:
        SetError(Error::NoSuchUser);
        break;
    }
}

void RoomMember::RoomMemberImpl::MemberLoop() {
    LOG_CRITICAL(Log, "run");
    client.run();
    LOG_CRITICAL(Log, "run ended");
}

void RoomMember::RoomMemberImpl::StartLoop() {
    loop_thread = std::make_unique<std::thread>(&RoomMember::RoomMemberImpl::MemberLoop, this);
}

void RoomMember::RoomMemberImpl::Send(Message&& message) {
    client.send(connection->get_handle(), message.GetData(), message.GetDataSize(),
                websocketpp::frame::opcode::binary);
}

void RoomMember::RoomMemberImpl::SendJoinRequest(const std::string& nickname, u64 console_id,
                                                 const MacAddress& preferred_mac,
                                                 const std::string& password) {
    Message message;
    message << static_cast<u8>(IdJoinRequest);
    message << nickname;
    message << console_id;
    message << preferred_mac;
    message << NetworkVersion;
    message << password;
    Send(std::move(message));
}

void RoomMember::RoomMemberImpl::HandleRoomInformationMessage(const std::vector<u8>& data) {
    Message message;
    message.Append(data.data(), data.size());
    // Ignore the first byte, which is the message type.
    message.IgnoreBytes(sizeof(u8)); // Ignore the message type
    message >> room_information.name;
    message >> room_information.description;
    message >> room_information.port;
    message >> room_information.creator;
    u32 num_members;
    message >> num_members;
    member_information.resize(num_members);
    for (auto& member : member_information) {
        message >> member.nickname;
        message >> member.mac_address;
        message >> member.program;
    }
    Invoke(room_information);
}

void RoomMember::RoomMemberImpl::HandleJoinMessage(const std::vector<u8>& data) {
    Message message;
    message.Append(data.data(), data.size());
    // Ignore the first byte, which is the message type.
    message.IgnoreBytes(sizeof(u8)); // Ignore the message type
    // Parse the MAC address from the message
    message >> mac_address;
}

void RoomMember::RoomMemberImpl::HandleWifiPacket(const std::vector<u8>& data) {
    WifiPacket wifi_packet;
    Message message;
    message.Append(data.data(), data.size());
    // Ignore the first byte, which is the message type.
    message.IgnoreBytes(sizeof(u8)); // Ignore the message type
    // Parse the WifiPacket from the message
    u8 frame_type;
    message >> frame_type;
    WifiPacket::PacketType type{static_cast<WifiPacket::PacketType>(frame_type)};
    wifi_packet.type = type;
    message >> wifi_packet.channel;
    message >> wifi_packet.transmitter_address;
    message >> wifi_packet.destination_address;
    message >> wifi_packet.data;
    Invoke<WifiPacket>(wifi_packet);
}

void RoomMember::RoomMemberImpl::HandleChatMessage(const std::vector<u8>& data) {
    Message message;
    message.Append(data.data(), data.size());
    // Ignore the first byte, which is the message type.
    message.IgnoreBytes(sizeof(u8));
    ChatEntry chat_entry;
    message >> chat_entry.nickname;
    message >> chat_entry.message;
    Invoke<ChatEntry>(chat_entry);
}

void RoomMember::RoomMemberImpl::HandleStatusMessageMessage(const std::vector<u8>& data) {
    Message message;
    message.Append(data.data(), data.size());
    // Ignore the first byte, which is the message type.
    message.IgnoreBytes(sizeof(u8));
    StatusMessageEntry status_message_entry;
    u8 type;
    message >> type;
    status_message_entry.type = static_cast<StatusMessageTypes>(type);
    message >> status_message_entry.nickname;
    Invoke<StatusMessageEntry>(status_message_entry);
}

void RoomMember::RoomMemberImpl::HandleModBanListResponseMessage(const std::vector<u8>& data) {
    Message message;
    message.Append(data.data(), data.size());
    // Ignore the first byte, which is the message type.
    message.IgnoreBytes(sizeof(u8));
    Room::BanList ban_list;
    message >> ban_list;
    Invoke<Room::BanList>(ban_list);
}

template <>
RoomMember::RoomMemberImpl::CallbackSet<WifiPacket>& RoomMember::RoomMemberImpl::Callbacks::Get() {
    return callback_set_wifi_packet;
}

template <>
RoomMember::RoomMemberImpl::CallbackSet<RoomMember::State>&
RoomMember::RoomMemberImpl::Callbacks::Get() {
    return callback_set_state;
}

template <>
RoomMember::RoomMemberImpl::CallbackSet<RoomMember::Error>&
RoomMember::RoomMemberImpl::Callbacks::Get() {
    return callback_set_error;
}

template <>
RoomMember::RoomMemberImpl::CallbackSet<RoomInformation>&
RoomMember::RoomMemberImpl::Callbacks::Get() {
    return callback_set_room_information;
}

template <>
RoomMember::RoomMemberImpl::CallbackSet<ChatEntry>& RoomMember::RoomMemberImpl::Callbacks::Get() {
    return callback_set_chat_message;
}

template <>
RoomMember::RoomMemberImpl::CallbackSet<StatusMessageEntry>&
RoomMember::RoomMemberImpl::Callbacks::Get() {
    return callback_set_status_message;
}

template <>
RoomMember::RoomMemberImpl::CallbackSet<Room::BanList>&
RoomMember::RoomMemberImpl::Callbacks::Get() {
    return callback_set_ban_list;
}

template <typename T>
void RoomMember::RoomMemberImpl::Invoke(const T& data) {
    std::lock_guard lock{callback_mutex};
    CallbackSet<T> callback_set{callbacks.Get<T>()};
    for (auto const& callback : callback_set)
        (*callback)(data);
}

template <typename T>
RoomMember::CallbackHandle<T> RoomMember::RoomMemberImpl::Bind(
    std::function<void(const T&)> callback) {
    std::lock_guard lock{callback_mutex};
    CallbackHandle<T> handle{std::make_shared<std::function<void(const T&)>>(callback)};
    callbacks.Get<T>().insert(handle);
    return handle;
}

// RoomMember
RoomMember::RoomMember() : room_member_impl{std::make_unique<RoomMemberImpl>()} {
    room_member_impl->client.init_asio();
    room_member_impl->client.set_close_handler([this](ConnectionHandle) {
        LOG_CRITICAL(Log, "client close");
        if (room_member_impl->state == State::Joined) {
            room_member_impl->SetState(State::Idle);
            room_member_impl->SetError(Error::LostConnection);
        }
    });
    room_member_impl->client.set_message_handler(
        [this](ConnectionHandle connection, Client::message_ptr msg) {
            LOG_CRITICAL(Log, "client msg");
            room_member_impl->HandleMessage(connection, msg);
        });
}

RoomMember::~RoomMember() {
    ASSERT_MSG(!IsConnected(), "RoomMember is being destroyed while connected");
    if (room_member_impl->loop_thread)
        Leave();
}

RoomMember::State RoomMember::GetState() const {
    return room_member_impl->state;
}

const RoomMember::MemberList& RoomMember::GetMemberInformation() const {
    return room_member_impl->member_information;
}

const std::string& RoomMember::GetNickname() const {
    return room_member_impl->nickname;
}

const MacAddress& RoomMember::GetMacAddress() const {
    ASSERT_MSG(IsConnected(), "Tried to get MAC address while not connected");
    return room_member_impl->mac_address;
}

RoomInformation RoomMember::GetRoomInformation() const {
    return room_member_impl->room_information;
}

void RoomMember::Join(const std::string& nickname, u64 console_id, const char* server_addr,
                      u16 server_port, const MacAddress& preferred_mac,
                      const std::string& password) {
    // If the member is connected, kill the connection first
    if (room_member_impl->loop_thread && room_member_impl->loop_thread->joinable())
        Leave();
    // If the thread isn't running but the ptr still exists, reset it
    else if (room_member_impl->loop_thread)
        room_member_impl->loop_thread.reset();
    room_member_impl->SetState(State::Joining);
    room_member_impl->client.set_open_handler([=](ConnectionHandle) {
        LOG_INFO(Log, "connected");
        room_member_impl->nickname = nickname;
        room_member_impl->SendJoinRequest(nickname, console_id, preferred_mac, password);
        SetProgram(room_member_impl->current_program);
        LOG_INFO(Log, "joining");
    });
    std::error_code ec;
    auto connection{room_member_impl->client.get_connection(
        fmt::format("ws://{}:{}", server_addr, server_port), ec)};
    if (ec) {
        LOG_CRITICAL(Log, "{}", ec.message());
        room_member_impl->SetState(State::Idle);
        room_member_impl->SetError(Error::CouldNotConnect);
        return;
    }
    LOG_INFO(Log, "connecting");
    room_member_impl->connection = room_member_impl->client.connect(connection);
    room_member_impl->StartLoop();
}

bool RoomMember::IsConnected() const {
    return room_member_impl->IsConnected();
}

void RoomMember::SendWifiPacket(const WifiPacket& wifi_packet) {
    Message message;
    message << static_cast<u8>(IdWifiPacket);
    message << static_cast<u8>(wifi_packet.type);
    message << wifi_packet.channel;
    message << wifi_packet.transmitter_address;
    message << wifi_packet.destination_address;
    message << wifi_packet.data;
    room_member_impl->Send(std::move(message));
}

void RoomMember::SendChatMessage(const std::string& message) {
    Message smessage;
    smessage << static_cast<u8>(IdChatMessage);
    smessage << message;
    room_member_impl->Send(std::move(smessage));
}

void RoomMember::SetProgram(const std::string& program) {
    room_member_impl->current_program = program;
    if (!IsConnected())
        return;
    Message message;
    message << static_cast<u8>(IdSetProgram);
    message << program;
    room_member_impl->Send(std::move(message));
}

void RoomMember::SendModerationRequest(RoomMessageTypes type, const std::string& nickname) {
    ASSERT_MSG(type == IdModKick || type == IdModBan || type == IdModUnban,
               "Type isn't a moderation request");
    if (!IsConnected())
        return;
    Message message;
    message << static_cast<u8>(type);
    message << nickname;
    room_member_impl->Send(std::move(message));
}

void RoomMember::RequestBanList() {
    if (!IsConnected())
        return;
    Message message;
    message << static_cast<u8>(IdModGetBanList);
    room_member_impl->Send(std::move(message));
}

RoomMember::CallbackHandle<RoomMember::State> RoomMember::BindOnStateChanged(
    std::function<void(const RoomMember::State&)> callback) {
    return room_member_impl->Bind(callback);
}

RoomMember::CallbackHandle<RoomMember::Error> RoomMember::BindOnError(
    std::function<void(const RoomMember::Error&)> callback) {
    return room_member_impl->Bind(callback);
}

RoomMember::CallbackHandle<WifiPacket> RoomMember::BindOnWifiPacketReceived(
    std::function<void(const WifiPacket&)> callback) {
    return room_member_impl->Bind(callback);
}

RoomMember::CallbackHandle<RoomInformation> RoomMember::BindOnRoomInformationChanged(
    std::function<void(const RoomInformation&)> callback) {
    return room_member_impl->Bind(callback);
}

RoomMember::CallbackHandle<ChatEntry> RoomMember::BindOnChatMessageReceived(
    std::function<void(const ChatEntry&)> callback) {
    return room_member_impl->Bind(callback);
}

RoomMember::CallbackHandle<StatusMessageEntry> RoomMember::BindOnStatusMessageReceived(
    std::function<void(const StatusMessageEntry&)> callback) {
    return room_member_impl->Bind(callback);
}

RoomMember::CallbackHandle<Room::BanList> RoomMember::BindOnBanListReceived(
    std::function<void(const Room::BanList&)> callback) {
    return room_member_impl->Bind(callback);
}

template <typename T>
void RoomMember::Unbind(CallbackHandle<T> handle) {
    std::lock_guard lock{room_member_impl->callback_mutex};
    room_member_impl->callbacks.Get<T>().erase(handle);
}

void RoomMember::Leave() {
    room_member_impl->SetState(State::Idle);
    room_member_impl->client.close(room_member_impl->connection, websocketpp::close::status::normal,
                                   "Leave");
    room_member_impl->loop_thread->join();
    room_member_impl->loop_thread.reset();
    room_member_impl->connection.reset();
    room_member_impl->Invoke(RoomInformation{});
}

template void RoomMember::Unbind(CallbackHandle<WifiPacket>);
template void RoomMember::Unbind(CallbackHandle<RoomMember::State>);
template void RoomMember::Unbind(CallbackHandle<RoomMember::Error>);
template void RoomMember::Unbind(CallbackHandle<RoomInformation>);
template void RoomMember::Unbind(CallbackHandle<ChatEntry>);
template void RoomMember::Unbind(CallbackHandle<StatusMessageEntry>);
template void RoomMember::Unbind(CallbackHandle<Room::BanList>);

} // namespace Network
