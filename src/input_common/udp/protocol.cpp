// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstddef>

#include "common/logging/log.h"
#include "input_common/udp/protocol.h"

namespace InputCommon {
namespace UDP {

static const size_t GetSizeOfResponseType(Type t);

namespace Response {

/**
 * Returns Type if the packet is valid, else none
 *
 * Note: Modifies the buffer to zero out the crc (since thats the easiest way to check without
 * copying the buffer)
 */
boost::optional<Type> Validate(u8* data, size_t size) {
    if (size < sizeof(Header)) {
        LOG_ERROR(Input, "Invalid UDP packet received");
        return boost::none;
    }
    Header header;
    std::memcpy(&header, data, sizeof(Header));
    if (header.magic != SERVER_MAGIC) {
        LOG_ERROR(Input, "UDP Packet has an unexpected magic value");
        return boost::none;
    }
    if (header.protocol_version != PROTOCOL_VERSION) {
        LOG_ERROR(Input, "UDP Packet protocol mismatch");
        return boost::none;
    }
    if (header.type < Type::Version || header.type > Type::PadData) {
        LOG_ERROR(Input, "UDP Packet is an unknown type");
        return boost::none;
    }

    // Packet size must equal sizeof(Header) + sizeof(Data)
    // and also verify that the packet info mentions the correct size. Since the spec includes the
    // type of the packet as part of the data, we need to include it in size calculations here
    // ie: payload_length == sizeof(T) + sizeof(Type)
    const size_t data_len = GetSizeOfResponseType(header.type);
    if (header.payload_length != data_len + sizeof(Type) || size < data_len + sizeof(Header)) {
        LOG_ERROR(
            Input,
            "UDP Packet payload length doesn't match. Received: {} PayloadLength: {} Expected: {}",
            size, header.payload_length, data_len + sizeof(Type));
        return boost::none;
    }

    const u32 crc32 = header.crc;
    boost::crc_32_type result;
    // zero out the crc in the buffer and then run the crc against it
    std::memset(&data[offsetof(Header, crc)], 0, sizeof(u32_le));

    result.process_bytes(data, data_len + sizeof(Header));
    if (crc32 != result.checksum()) {
        LOG_ERROR(Input, "UDP Packet CRC check failed. Offset: {}", offsetof(Header, crc));
        return boost::none;
    }
    return header.type;
}
} // namespace Response

static const size_t GetSizeOfResponseType(Type t) {
    switch (t) {
    case Type::Version:
        return sizeof(Response::Version);
    case Type::PortInfo:
        return sizeof(Response::PortInfo);
    case Type::PadData:
        return sizeof(Response::PadData);
    }
    return 0;
}

} // namespace UDP
} // namespace InputCommon
