#pragma once

#include <filesystem>

#include "common/audio.h"
#include "common/error.h"

namespace echo::engine {

// 读取 16-bit PCM wav(任意采样率, 多声道会被合并为单声道)
[[nodiscard]] Result<AudioBuffer> ReadWavFile(const std::filesystem::path& file);

// 写出 16-bit PCM 单声道 wav
[[nodiscard]] Result<void> WriteWavFile(const std::filesystem::path& file,
                                        const AudioBuffer& audio);

}  // namespace echo::engine
