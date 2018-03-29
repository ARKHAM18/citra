// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <cstring>
#include "core/core_timing.h"
#include "core/hle/service/ptm/ptm.h"
#include "core/hle/shared_page.h"
#include "core/settings.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace SharedPage {

std::unique_ptr<SharedPage::Module> shared_page_handler;

static std::time_t GetInitTime() {
    switch (Settings::values.init_clock) {
    case Settings::InitClock::SystemTime: {
        auto now = std::chrono::system_clock::now();
        // If the system time is in daylight saving, we give an additional hour to console time
        std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm* now_tm = std::localtime(&now_time_t);
        if (now_tm && now_tm->tm_isdst > 0)
            now_time_t += 60 * 60 * 1000;
        return now_time_t;
    }
    case Settings::InitClock::FixedTime:
        return Settings::values.init_time;
    }
}

/// Gets system time in 3DS format. The epoch is Jan 1900, and the unit is millisecond.
u64 Module::GetSystemTime() {
    std::time_t now = init_time + CoreTiming::GetGlobalTimeUs() / 1000000;

    // 3DS system does't allow user to set a time before Jan 1 2000,
    // so we use it as an auxiliary epoch to calculate the console time.
    std::tm epoch_tm;
    epoch_tm.tm_sec = 0;
    epoch_tm.tm_min = 0;
    epoch_tm.tm_hour = 0;
    epoch_tm.tm_mday = 1;
    epoch_tm.tm_mon = 0;
    epoch_tm.tm_year = 100;
    epoch_tm.tm_isdst = 0;
    std::time_t epoch = std::mktime(&epoch_tm);

    // 3DS console time uses Jan 1 1900 as internal epoch,
    // so we use the milliseconds between 1900 and 2000 as base console time
    u64 console_time = 3155673600000ULL;

    // Only when system time is after 2000, we set it as 3DS system time
    if (now > epoch) {
        console_time += (now - epoch) * 1000;
    }

    return console_time;
}

void Module::UpdateTimeCallback(u64 userdata, int cycles_late) {
    DateTime& date_time =
        shared_page.date_time_counter % 2 ? shared_page.date_time_0 : shared_page.date_time_1;

    date_time.date_time = GetSystemTime();
    date_time.update_tick = CoreTiming::GetTicks();
    date_time.tick_to_second_coefficient = BASE_CLOCK_RATE_ARM11;
    date_time.tick_offset = 0;

    ++shared_page.date_time_counter;

    // system time is updated hourly
    CoreTiming::ScheduleEvent(msToCycles(60 * 60 * 1000) - cycles_late, update_time_event);
}

Module::Module() {
    std::memset(&shared_page, 0, sizeof(shared_page));

    shared_page.running_hw = 0x1; // product

    // Some games wait until this value becomes 0x1, before asking running_hw
    shared_page.unknown_value = 0x1;

    shared_page.battery_state.charge_level.Assign(
        static_cast<u8>(Settings::values.p_battery_level));
    shared_page.battery_state.is_adapter_connected.Assign(
        static_cast<u8>(Settings::values.p_adapter_connected));
    shared_page.battery_state.is_charging.Assign(
        static_cast<u8>(Settings::values.p_battery_charging));

    shared_page.wifi_link_level = Settings::values.n_wifi_link_level;
    shared_page.network_state = Settings::values.n_state;

    shared_page.ledstate_3d = Settings::values.sp_enable_3d ? 0 : 1;

    init_time = GetInitTime();

    using namespace std::placeholders;
    update_time_event = CoreTiming::RegisterEvent(
        "SharedPage::UpdateTimeCallback", std::bind(&Module::UpdateTimeCallback, this, _1, _2));
    CoreTiming::ScheduleEvent(0, update_time_event);
}

void Module::SetMacAddress(const MacAddress& addr) {
    std::memcpy(shared_page.wifi_macaddr, addr.data(), sizeof(MacAddress));
}

} // namespace SharedPage
