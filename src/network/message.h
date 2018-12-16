// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <vector>
#include "common/common_types.h"

namespace Network {

/// A class that serializes data for network transfer. It also handles endianess
class Message {
public:
    Message() = default;
    ~Message() = default;

    /**
     * Append data to the end of the message
     * @param data Pointer to the sequence of bytes to append
     * @param size_in_bytes Number of bytes to append
     */
    void Append(const void* data, std::size_t size_in_bytes);

    /**
     * Reads data from the current read position of the message
     * @param out_data Pointer where the data should get written to
     * @param size_in_bytes Number of bytes to read
     */
    void Read(void* out_data, std::size_t size_in_bytes);

    /**
     * Clear the message
     * After calling Clear, the message is empty.
     */
    void Clear();

    /**
     * Ignores bytes while reading
     * @param length THe number of bytes to ignore
     */
    void IgnoreBytes(u32 length);

    /**
     * Get a pointer to the data contained in the message
     * @return Pointer to the data
     */
    const void* GetData() const;

    /**
     * This function returns the number of bytes pointed to by
     * what getData returns.
     * @return Data size, in bytes
     */
    std::size_t GetDataSize() const;

    /**
     * This function is useful to know if there is some data
     * left to be read, without actually reading it.
     * @return True if all data was read, false otherwise
     */
    bool EndOfMessage() const;

    explicit operator bool() const;

    /// Overloads of operator >> to read data from the message
    Message& operator>>(bool& out_data);
    Message& operator>>(s8& out_data);
    Message& operator>>(u8& out_data);
    Message& operator>>(s16& out_data);
    Message& operator>>(u16& out_data);
    Message& operator>>(s32& out_data);
    Message& operator>>(u32& out_data);
    Message& operator>>(s64& out_data);
    Message& operator>>(u64& out_data);
    Message& operator>>(float& out_data);
    Message& operator>>(double& out_data);
    Message& operator>>(char* out_data);
    Message& operator>>(std::string& out_data);
    template <typename T>
    Message& operator>>(std::vector<T>& out_data);
    template <typename T, std::size_t S>
    Message& operator>>(std::array<T, S>& out_data);

    /// Overloads of operator << to write data into the message
    Message& operator<<(bool in_data);
    Message& operator<<(s8 in_data);
    Message& operator<<(u8 in_data);
    Message& operator<<(s16 in_data);
    Message& operator<<(u16 in_data);
    Message& operator<<(s32 in_data);
    Message& operator<<(u32 in_data);
    Message& operator<<(s64 in_data);
    Message& operator<<(u64 in_data);
    Message& operator<<(float in_data);
    Message& operator<<(double in_data);
    Message& operator<<(const char* in_data);
    Message& operator<<(const std::string& in_data);
    template <typename T>
    Message& operator<<(const std::vector<T>& in_data);
    template <typename T, std::size_t S>
    Message& operator<<(const std::array<T, S>& data);

private:
    /**
     * Check if the message can extract a given number of bytes
     * This function updates accordingly the state of the message.
     * @param size Size to check
     * @return True if size bytes can be read from the message
     */
    bool CheckSize(std::size_t size);

    // Member data
    std::vector<char> data; ///< Data stored in the message
    std::size_t read_pos{}; ///< Current reading position in the message
    bool is_valid{true};    ///< Reading state of the message
};

template <typename T>
Message& Message::operator>>(std::vector<T>& out_data) {
    // First extract the size
    u32 size;
    *this >> size;
    out_data.resize(size);
    // Then extract the data
    for (std::size_t i{}; i < out_data.size(); ++i) {
        T character;
        *this >> character;
        out_data[i] = character;
    }
    return *this;
}

template <typename T, std::size_t S>
Message& Message::operator>>(std::array<T, S>& out_data) {
    for (std::size_t i{}; i < out_data.size(); ++i) {
        T character;
        *this >> character;
        out_data[i] = character;
    }
    return *this;
}

template <typename T>
Message& Message::operator<<(const std::vector<T>& in_data) {
    // First insert the size
    *this << static_cast<u32>(in_data.size());
    // Then insert the data
    for (std::size_t i{}; i < in_data.size(); ++i)
        *this << in_data[i];
    return *this;
}

template <typename T, std::size_t S>
Message& Message::operator<<(const std::array<T, S>& in_data) {
    for (std::size_t i{}; i < in_data.size(); ++i)
        *this << in_data[i];
    return *this;
}

} // namespace Network
