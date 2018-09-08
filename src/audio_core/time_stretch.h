// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include "common/common_types.h"

namespace soundtouch {
class SoundTouch;
} // namespace soundtouch

namespace AudioCore {

class TimeStretcher {
public:
    TimeStretcher();
    ~TimeStretcher();

    void SetOutputSampleRate(size_t sample_rate);

    /// @param in       Input sample buffer
    /// @param num_in   Number of input frames in `in`
    /// @param out      Output sample buffer
    /// @param num_out  Desired number of output frames in `out`
    /// @returns Actual number of frames written to `out`
    size_t Process(const s16* in, size_t num_in, s16* out, size_t num_out);

    void Clear();

    void Flush();

private:
    size_t sample_rate;
    std::unique_ptr<soundtouch::SoundTouch> sound_touch;
    double stretch_ratio{1.0};
};

} // namespace AudioCore
