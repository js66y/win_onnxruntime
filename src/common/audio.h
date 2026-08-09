#pragma once

#include <vector>

namespace echo {

// 单声道 PCM 音频, 采样值范围 [-1, 1]
struct AudioBuffer {
  std::vector<float> samples;
  int sample_rate = 0;

  [[nodiscard]] double DurationSeconds() const {
    return sample_rate > 0
               ? static_cast<double>(samples.size()) / sample_rate
               : 0.0;
  }
};

}  // namespace echo
