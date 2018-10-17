// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <memory>
#include "common/common_types.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/service.h"
extern "C" {
#include "nfc3d/amitool.h"
}

namespace Kernel {
class Event;
} // namespace Kernel

namespace Service::NFC {

namespace ErrCodes {
enum {
    CommandInvalidForState = 512,
};
} // namespace ErrCodes

enum class TagState : u8 {
    NotInitialized = 0,
    NotScanning = 1,
    Scanning = 2,
    TagInRange = 3,
    TagOutOfRange = 4,
    TagDataLoaded = 5,
    Unknown6 = 6,
};

enum class CommunicationStatus : u8 {
    AttemptInitialize = 1,
    NfcInitialized = 2,
};

class Module final {
public:
    Module();
    ~Module();

    class Interface : public ServiceFramework<Interface> {
    public:
        Interface(std::shared_ptr<Module> nfc, const char* name);
        ~Interface();
        void LoadAmiibo(const std::string& path);
        void RemoveAmiibo();

    protected:
        void Initialize(Kernel::HLERequestContext& ctx);
        void Shutdown(Kernel::HLERequestContext& ctx);
        void StartCommunication(Kernel::HLERequestContext& ctx);
        void StopCommunication(Kernel::HLERequestContext& ctx);
        void StartTagScanning(Kernel::HLERequestContext& ctx);
        void StopTagScanning(Kernel::HLERequestContext& ctx);
        void LoadAmiiboData(Kernel::HLERequestContext& ctx);
        void ResetTagScanState(Kernel::HLERequestContext& ctx);
        void GetTagInRangeEvent(Kernel::HLERequestContext& ctx);
        void GetTagOutOfRangeEvent(Kernel::HLERequestContext& ctx);
        void GetTagState(Kernel::HLERequestContext& ctx);
        void CommunicationGetStatus(Kernel::HLERequestContext& ctx);
        void GetTagInfo(Kernel::HLERequestContext& ctx);
        void GetAmiiboSettings(Kernel::HLERequestContext& ctx);
        void GetAmiiboConfig(Kernel::HLERequestContext& ctx);
        void OpenAppData(Kernel::HLERequestContext& ctx);
        void ReadAppData(Kernel::HLERequestContext& ctx);
        void Unknown1(Kernel::HLERequestContext& ctx);
        void Unknown0x1A(Kernel::HLERequestContext& ctx);
        void GetIdentificationBlock(Kernel::HLERequestContext& ctx);

    private:
        std::shared_ptr<Module> nfc;
    };

private:
    Kernel::SharedPtr<Kernel::Event> tag_in_range_event;
    Kernel::SharedPtr<Kernel::Event> tag_out_of_range_event;
    std::atomic<TagState> nfc_tag_state{TagState::NotInitialized};
    CommunicationStatus nfc_status{CommunicationStatus::NfcInitialized};
    std::array<u8, 160> keys{};
    std::array<u8, AMIIBO_MAX_SIZE> encrypted_data{};
    std::array<u8, AMIIBO_MAX_SIZE> decrypted_data{};
};

void InstallInterfaces(SM::ServiceManager& service_manager);

} // namespace Service::NFC
