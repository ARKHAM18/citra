// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <functional>
#include "common/common_types.h"

namespace RPC {

enum class PacketType {
    Undefined = 0,
    ReadMemory,
    WriteMemory,
    PadState,
    TouchState,
    MotionState,
    CircleState,
    SetResolution,
    SetGame,
    SetOverrideControls,
    Pause,
    Resume,
    Restart,
    SetSpeedLimit,
    SetBackgroundColor,
};

struct PacketHeader {
    u32 version;
    u32 id;
    PacketType packet_type;
    u32 packet_size;
};

constexpr u32 CURRENT_VERSION = 1;
constexpr u32 MIN_PACKET_SIZE = sizeof(PacketHeader);

class Packet {
public:
    Packet(const PacketHeader& header, u8* data, std::function<void(Packet&)> send_reply_callback);

    u32 GetVersion() const {
        return header.version;
    }

    u32 GetId() const {
        return header.id;
    }

    PacketType GetPacketType() const {
        return header.packet_type;
    }

    u32 GetPacketDataSize() const {
        return header.packet_size;
    }

    const PacketHeader& GetHeader() const {
        return header;
    }

    std::vector<u8>& GetPacketData() {
        return packet_data;
    }

    void SetPacketDataSize(u32 size) {
        header.packet_size = size;
    }

    void SendReply() {
        send_reply_callback(*this);
    }

private:
    void HandleReadMemory(u32 address, u32 data_size);
    void HandleWriteMemory(u32 address, const u8* data, u32 data_size);
    void HandlePadState(Packet& packet, u32 raw);
    void HandleTouchState(Packet& packet, s16 x, s16 y, bool valid);
    void HandleMotionState(Packet& packet, s16 x, s16 y, s16 z, s16 roll, s16 pitch, s16 yaw);
    void HandleCircleState(Packet& packet, s16 x, s16 y);
    void HandleSetResolution(Packet& packet, u16 resolution);
    void HandleSetGame(Packet& packet, const std::string& path);
    void HandleSetOverrideControls(Packet& packet, bool pad, bool touch, bool motion, bool circle);
    void HandlePause(Packet& packet);
    void HandleResume(Packet& packet);
    void HandleRestart(Packet& packet);
    void HandleSetSpeedLimit(Packet& packet, u16 speed_limit);
    void HandleSetBackgroundColor(Packet& packet, float r, float g, float b);

    struct PacketHeader header;
    std::vector<u8> packet_data;

    std::function<void(Packet&)> send_reply_callback;
};

} // namespace RPC
