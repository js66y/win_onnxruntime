#include <gtest/gtest.h>

#include "dialog/tools.h"

namespace {

using echo::dialog::ToolRegistry;

TEST(ToolRegistryTest, MatchesTimeKeywords) {
  ToolRegistry tools;
  auto result = tools.TryHandle("现在几点了?");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->tool_name, "time");
  EXPECT_NE(result->display.find("现在是"), std::string::npos);
}

TEST(ToolRegistryTest, EvaluatesExpression) {
  ToolRegistry tools;
  auto result = tools.TryHandle("计算 12*(3+4)");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->tool_name, "calc");
  EXPECT_NE(result->display.find("84"), std::string::npos);
}

TEST(ToolRegistryTest, PureExpressionWorks) {
  ToolRegistry tools;
  auto result = tools.TryHandle("3+5*2");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->tool_name, "calc");
  EXPECT_NE(result->display.find("13"), std::string::npos);
}

TEST(ToolRegistryTest, MatchesSysInfo) {
  ToolRegistry tools;
  auto result = tools.TryHandle("系统信息怎么样");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->tool_name, "sysinfo");
  EXPECT_NE(result->display.find("逻辑处理器"), std::string::npos);
}

TEST(ToolRegistryTest, FallsThroughToLlm) {
  ToolRegistry tools;
  EXPECT_FALSE(tools.TryHandle("讲个笑话").has_value());
}

}  // namespace
