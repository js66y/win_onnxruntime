#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "common/config.h"

namespace {

namespace fs = std::filesystem;

// 在临时目录写一个配置文件, 测试结束自动清理
class ConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = fs::temp_directory_path() / "echo_config_test";
    fs::create_directories(dir_);
  }
  void TearDown() override { fs::remove_all(dir_); }

  fs::path WriteConfig(std::string_view content) {
    const auto file = dir_ / "echo.json";
    std::ofstream out(file);
    out << content;
    return file;
  }

  fs::path dir_;
};

TEST_F(ConfigTest, LoadsAllSectionsAndResolvesRelativePaths) {
  const auto file = WriteConfig(R"({
    "llm": { "model_dir": "third_party/qwen", "temperature": 0.5 },
    "asr": { "model": "models/asr/model.onnx", "tokens": "models/asr/tokens.txt" },
    "tts": { "model": "models/tts/model.onnx", "speed": 1.2 },
    "vad": { "model": "models/silero_vad.onnx", "threshold": 0.6 },
    "system_prompt": "测试提示词"
  })");

  const auto config = echo::AppConfig::LoadFile(file);
  ASSERT_TRUE(config.has_value()) << config.error().message;

  // 相对路径应基于配置文件目录解析为绝对路径
  EXPECT_EQ(config->llm.model_dir, dir_ / "third_party" / "qwen");
  EXPECT_EQ(config->asr.model, dir_ / "models" / "asr" / "model.onnx");
  EXPECT_FLOAT_EQ(config->llm.temperature, 0.5f);
  EXPECT_FLOAT_EQ(config->tts.speed, 1.2f);
  EXPECT_FLOAT_EQ(config->vad.threshold, 0.6f);
  EXPECT_EQ(config->system_prompt, "测试提示词");
}

TEST_F(ConfigTest, KeepsDefaultsForMissingFields) {
  const auto file = WriteConfig(R"({ "llm": { "model_dir": "m" } })");

  const auto config = echo::AppConfig::LoadFile(file);
  ASSERT_TRUE(config.has_value()) << config.error().message;

  EXPECT_EQ(config->llm.max_length, 4096);
  EXPECT_TRUE(config->llm.disable_thinking);
  EXPECT_EQ(config->asr.language, "auto");
  EXPECT_FLOAT_EQ(config->vad.min_silence_seconds, 0.3f);
  EXPECT_FALSE(config->system_prompt.empty());
}

TEST_F(ConfigTest, FailsWhenFileMissing) {
  const auto config = echo::AppConfig::LoadFile(dir_ / "not_exist.json");
  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, echo::ErrorCode::kConfigNotFound);
}

TEST_F(ConfigTest, FailsOnInvalidJson) {
  const auto file = WriteConfig("{ not valid json !!");
  const auto config = echo::AppConfig::LoadFile(file);
  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, echo::ErrorCode::kConfigInvalid);
}

TEST_F(ConfigTest, FailsWhenModelDirMissing) {
  const auto file = WriteConfig(R"({ "llm": {} })");
  const auto config = echo::AppConfig::LoadFile(file);
  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, echo::ErrorCode::kConfigInvalid);
}

}  // namespace
