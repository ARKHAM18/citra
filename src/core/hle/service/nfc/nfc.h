// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "common/common_types.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/service.h"

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
        void GetAmiiboConfig(Kernel::HLERequestContext& ctx);
        void Unknown1(Kernel::HLERequestContext& ctx);

    private:
        std::shared_ptr<Module> nfc;
    };

private:
    Kernel::SharedPtr<Kernel::Event> tag_out_of_range_event;
    CommunicationStatus nfc_status{CommunicationStatus::NfcInitialized};
};

void InstallInterfaces(SM::ServiceManager& service_manager);

} // namespace Service::NFC
