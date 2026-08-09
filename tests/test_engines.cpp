// 语音引擎集成测试: 依赖 models/ 下的真实模型, 模型缺失时跳过(GTEST_SKIP)。
#include <windows.h>

#include <filesystem>

#include <gtest/gtest.h>

#include "common/config.h"
#include "engine/asr/sherpa_asr.h"
#include "engine/audio_io.h"
#include "engine/tts/sherpa_tts.h"
#include "engine/vad/silero_vad.h"

namespace {

namespace fs = std::filesystem;

// 测试 exe 位于 build/bin/<Config>/, 仓库根在其上三级
fs::path RepoRoot() {
  wchar_t buffer[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  return fs::path(buffer).parent_path() / ".." / ".." / "..";
}

echo::AsrConfig MakeAsrConfig() {
  const auto dir =
      RepoRoot() / "models" / "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17";
  return {.model = dir / "model.int8.onnx", .tokens = dir / "tokens.txt"};
}

echo::TtsConfig MakeTtsConfig() {
  const auto dir = RepoRoot() / "models" / "vits-melo-tts-zh_en";
  return {.model = dir / "model.onnx",
          .lexicon = dir / "lexicon.txt",
          .tokens = dir / "tokens.txt",
          .dict_dir = dir / "dict"};
}

echo::VadConfig MakeVadConfig() {
  return {.model = RepoRoot() / "models" / "silero_vad.onnx"};
}

#define SKIP_IF_MISSING(path)                                              \
  if (!fs::exists(path)) {                                                 \
    GTEST_SKIP() << "模型未下载, 跳过: " << (path).string();               \
  }

TEST(SherpaAsrTest, RecognizesChineseTestWav) {
  const auto config = MakeAsrConfig();
  SKIP_IF_MISSING(config.model);

  const auto wav_path = config.model.parent_path() / "test_wavs" / "zh.wav";
  SKIP_IF_MISSING(wav_path);

  auto asr = echo::engine::SherpaAsr::Create(config);
  ASSERT_TRUE(asr.has_value()) << asr.error().message;

  auto audio = echo::engine::ReadWavFile(wav_path);
  ASSERT_TRUE(audio.has_value()) << audio.error().message;

  auto result = (*asr)->Recognize(audio->samples, audio->sample_rate);
  ASSERT_TRUE(result.has_value()) << result.error().message;

  // zh.wav 实际内容: "开放时间早上9点至下午5点"
  EXPECT_NE(result->text.find("开放时间"), std::string::npos)
      << "识别结果: " << result->text;
  EXPECT_EQ(result->lang, "zh");
}

TEST(SherpaAsrTest, FailsOnEmptyInput) {
  const auto config = MakeAsrConfig();
  SKIP_IF_MISSING(config.model);

  auto asr = echo::engine::SherpaAsr::Create(config);
  ASSERT_TRUE(asr.has_value()) << asr.error().message;

  auto result = (*asr)->Recognize({}, 16000);
  EXPECT_FALSE(result.has_value());
}

TEST(SherpaTtsTest, SynthesizesNonEmptyAudio) {
  const auto config = MakeTtsConfig();
  SKIP_IF_MISSING(config.model);

  auto tts = echo::engine::SherpaTts::Create(config);
  ASSERT_TRUE(tts.has_value()) << tts.error().message;

  auto audio = (*tts)->Synthesize("你好, 世界!");
  ASSERT_TRUE(audio.has_value()) << audio.error().message;

  EXPECT_FALSE(audio->samples.empty());
  EXPECT_GT(audio->sample_rate, 0);
  EXPECT_GT(audio->DurationSeconds(), 0.3);
}

// 关键闭环: 合成的中文再被识别回来, 同时验证 TTS 正确性与
// "genai 的 onnxruntime.dll(1.28) 同时服务 sherpa(按 1.27 编译)" 的兼容性
TEST(SpeechPipelineTest, TtsAsrRoundTrip) {
  const auto tts_config = MakeTtsConfig();
  const auto asr_config = MakeAsrConfig();
  SKIP_IF_MISSING(tts_config.model);
  SKIP_IF_MISSING(asr_config.model);

  auto tts = echo::engine::SherpaTts::Create(tts_config);
  ASSERT_TRUE(tts.has_value()) << tts.error().message;
  auto asr = echo::engine::SherpaAsr::Create(asr_config);
  ASSERT_TRUE(asr.has_value()) << asr.error().message;

  auto audio = (*tts)->Synthesize("今天天气真不错");
  ASSERT_TRUE(audio.has_value()) << audio.error().message;

  auto result = (*asr)->Recognize(audio->samples, audio->sample_rate);
  ASSERT_TRUE(result.has_value()) << result.error().message;

  EXPECT_NE(result->text.find("天气"), std::string::npos)
      << "识别结果: " << result->text;
}

TEST(SileroVadTest, DetectsSpeechSegmentInTestWav) {
  const auto vad_config = MakeVadConfig();
  const auto asr_config = MakeAsrConfig();
  SKIP_IF_MISSING(vad_config.model);

  // zh.wav 是 16kHz, 正好匹配 VAD 的输入要求
  const auto wav_path =
      asr_config.model.parent_path() / "test_wavs" / "zh.wav";
  SKIP_IF_MISSING(wav_path);

  auto vad = echo::engine::SileroVad::Create(vad_config);
  ASSERT_TRUE(vad.has_value()) << vad.error().message;

  auto audio = echo::engine::ReadWavFile(wav_path);
  ASSERT_TRUE(audio.has_value()) << audio.error().message;
  ASSERT_EQ(audio->sample_rate, 16000);

  // 模拟实时场景: 按 20ms 一帧送入
  constexpr size_t kFrame = 320;
  size_t offset = 0;
  int segments = 0;
  while (offset < audio->samples.size()) {
    const auto n = std::min(kFrame, audio->samples.size() - offset);
    (*vad)->Feed({audio->samples.data() + offset, n});
    offset += n;
    while ((*vad)->PopSegment()) ++segments;
  }
  (*vad)->Flush();
  while ((*vad)->PopSegment()) ++segments;

  EXPECT_GE(segments, 1) << "应至少检测到一段语音";
}

}  // namespace
