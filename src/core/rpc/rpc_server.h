// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include "common/threadsafe_queue.h"
#include "core/rpc/server.h"

namespace RPC {

class RPCServer {
public:
    RPCServer();
    ~RPCServer();

    void QueueRequest(std::unique_ptr<RPC::Packet> request);

    static inline std::function<void()> cb_update_frame_advancing;

private:
    void Start();
    void Stop();
    void HandleReadMemory(Packet& packet, u32 address, u32 data_size);
    void HandleWriteMemory(Packet& packet, u32 address, const u8* data, u32 data_size);
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
    void HandleSetScreenRefreshRate(Packet& packet, int rate);
    void HandleSetShadowsEnabled(Packet& packet, bool enabled);
    void HandleIsButtonPressed(Packet& packet, int button);
    void HandleSetFrameAdvancing(Packet& packet, bool enable);
    void HandleAdvanceFrame(Packet& packet);
    bool ValidatePacket(const PacketHeader& packet_header);
    void HandleSingleRequest(std::unique_ptr<Packet> request);
    void HandleRequestsLoop();

    Server server;
    Common::SPSCQueue<std::unique_ptr<Packet>> request_queue;
    std::thread request_handler_thread;
    std::mutex request_queue_mutex;
};

} // namespace RPC
