#pragma once

#include "engine/MediaWaveform.h"

namespace drift {

// Integrated loudness (ITU-R BS.1770 / EBU R128, simplified) plus a 4×-interpolated
// true-peak estimate. Built on the same pull-chunk PCM source MediaWaveform uses so a
// timeline mix and a clip decode share one code path.
struct LoudnessResult
{
    bool ok = false;
    double integratedLufs = 0.0;
    double truePeakDb = -120.0;
    double durationSeconds = 0.0;
};

LoudnessResult measureLoudness(qint64 totalFrames, int sampleRate,
                               const MediaWaveform::FillChunk &fill);

} // namespace drift
