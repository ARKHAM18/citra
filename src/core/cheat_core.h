// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <map>
#include <memory>
#include <vector>
#include "common/string_util.h"
#include "core/core_timing.h"

namespace CheatCore {
/*
 * Starting point for running cheat codes. Initializes tick event and executes cheats every tick.
 */
void Init();
void Shutdown();
void RefreshCheats();
} // namespace CheatCore

namespace CheatEngine {

enum class CheatType {
    Null = -0x1,
    Write32 = 0x00,
    Write16 = 0x01,
    Write8 = 0x02,
    GreaterThan32 = 0x03,
    LessThan32 = 0x04,
    EqualTo32 = 0x05,
    NotEqualTo32 = 0x06,
    GreaterThan16 = 0x07,
    LessThan16 = 0x08,
    EqualTo16 = 0x09,
    NotEqualTo16 = 0x0A,
    LoadOffset = 0x0B,
    Loop = 0x0C,
    Terminator = 0xD0,
    LoopExecuteVariant = 0xD1,
    FullTerminator = 0xD2,
    SetOffset = 0xD3,
    AddValue = 0xD4,
    SetValue = 0xD5,
    IncrementiveWrite32 = 0xD6,
    IncrementiveWrite16 = 0xD7,
    IncrementiveWrite8 = 0xD8,
    Load32 = 0xD9,
    Load16 = 0xDA,
    Load8 = 0xDB,
    AddOffset = 0xDC,
    Joker = 0xDD,
    Patch = 0x0E,
};

/// Represents a single line of a cheat, i.e. 1xxxxxxxx yyyyyyyy
struct CheatLine {
    explicit CheatLine(std::string line) {
        line = std::string(line.c_str()); // remove '/0' characters if any.
        line = Common::Trim(line);
        constexpr int cheat_length = 17;
        if (line.length() != cheat_length) {
            type = CheatType::Null;
            cheat_line = line;
            return;
        }
        try {
            std::string type_temp = line.substr(0, 1);

            // 0xD types have extra subtype value, i.e. 0xDA
            std::string sub_type_temp;
            if (type_temp == "D" || type_temp == "d")
                sub_type_temp = line.substr(1, 1);
            type = static_cast<CheatType>(std::stoi(type_temp + sub_type_temp, 0, 16));
            address = std::stoi(line.substr(1, 8), 0, 16);
            value = std::stoi(line.substr(10, 8), 0, 16);
            cheat_line = line;
        } catch (const std::exception&) {
            type = CheatType::Null;
            cheat_line = line;
            return;
        }
    }

    CheatType type;
    u32 address;
    u32 value;
    std::string cheat_line;
};

/// Base interface for all types of cheats.
class CheatBase {
public:
    virtual void Execute() = 0;
    virtual ~CheatBase() = default;
    virtual std::string ToString() = 0;

    bool GetEnabled() const {
        return enabled;
    }

    void SetEnabled(bool enabled) {
        this->enabled = enabled;
    }

    const std::vector<CheatLine>& GetCheatLines() const {
        return cheat_lines;
    }

    void SetCheatLines(std::vector<CheatLine> new_lines) {
        cheat_lines = std::move(new_lines);
    }

    const std::string& GetName() const {
        return name;
    }

    void SetName(std::string new_name) {
        name = std::move(new_name);
    }

    virtual std::string GetType() = 0;

protected:
    bool enabled = false;
    std::vector<CheatLine> cheat_lines;
    std::string name;
};

/// Implements support for Gateway (GateShark) cheats.
class GatewayCheat : public CheatBase {
public:
    explicit GatewayCheat(std::string name) {
        this->name = std::move(name);
    }

    explicit GatewayCheat(std::vector<CheatLine> cheat_lines, bool enabled, std::string name) {
        this->name = std::move(name);
        this->cheat_lines = std::move(cheat_lines);
        this->enabled = enabled;
    }

    void Execute() override;
    std::string ToString() override;

    std::string GetType() override {
        return "Gateway";
    }
};

/// Handles loading/saving of cheats and executing them.
class CheatEngine {
public:
    CheatEngine();
    void Run();
    static std::vector<std::shared_ptr<CheatBase>> ReadFileContents();
    static void Save(std::vector<std::shared_ptr<CheatBase>> cheats);
    void RefreshCheats();

private:
    std::vector<std::shared_ptr<CheatBase>> cheats_list;
};
} // namespace CheatEngine
