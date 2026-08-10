#include <gtest/gtest.h>

#include "dialog/sentence_splitter.h"

namespace {

using echo::dialog::SentenceSplitter;
using echo::dialog::StripForTts;

TEST(SentenceSplitterTest, CutsAtChinesePunctuation) {
  SentenceSplitter splitter;
  auto out = splitter.Feed("今天天气很好。我们出去玩吧!");
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0], "今天天气很好。");
  EXPECT_EQ(out[1], "我们出去玩吧!");
  EXPECT_TRUE(splitter.Flush().empty());
}

TEST(SentenceSplitterTest, HandlesStreamingDeltas) {
  SentenceSplitter splitter;
  std::vector<std::string> sentences;
  // 模拟 LLM 逐 token 输出, 甚至从多字节字符中间切开
  const std::string full = "你好呀,很高兴认识你。有什么我能帮忙的吗?";
  for (size_t i = 0; i < full.size(); i += 2) {  // 2 字节一段, 撕裂 UTF-8
    for (auto& s : splitter.Feed(full.substr(i, 2))) {
      sentences.push_back(std::move(s));
    }
  }
  if (auto tail = splitter.Flush(); !tail.empty()) {
    sentences.push_back(std::move(tail));
  }
  ASSERT_EQ(sentences.size(), 2u);
  EXPECT_EQ(sentences[0], "你好呀,很高兴认识你。");
  EXPECT_EQ(sentences[1], "有什么我能帮忙的吗?");
}

TEST(SentenceSplitterTest, TooShortSentenceWaitsForMore) {
  SentenceSplitter splitter(/*min_bytes=*/12);
  EXPECT_TRUE(splitter.Feed("好。").empty());  // 6 字节 < 12, 不切
  auto out = splitter.Feed("我知道了。");
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0], "好。我知道了。");
}

TEST(SentenceSplitterTest, LongRunWithoutTerminalCutsAtComma) {
  SentenceSplitter splitter(/*min_bytes=*/12, /*soft_max_bytes=*/30);
  // 长句没有句号, 超过 soft_max 后逗号也切
  auto out = splitter.Feed("这是一个特别特别长的句子,后面还有很多字");
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0], "这是一个特别特别长的句子,");
  EXPECT_EQ(splitter.Flush(), "后面还有很多字");
}

TEST(SentenceSplitterTest, FlushReturnsRemainder) {
  SentenceSplitter splitter;
  EXPECT_TRUE(splitter.Feed("没有标点的尾巴").empty());
  EXPECT_EQ(splitter.Flush(), "没有标点的尾巴");
}

TEST(StripForTtsTest, RemovesMarkdownMarkers) {
  EXPECT_EQ(StripForTts("**你好** `代码` # 标题"), "你好 代码  标题");
  EXPECT_EQ(StripForTts("正常文本。"), "正常文本。");
}

TEST(StripForTtsTest, ConvertsNumbersToChinese) {
  // melo-tts 词典没有阿拉伯数字, 必须转中文读法
  EXPECT_EQ(StripForTts("早上9点到下午5点"), "早上九点到下午五点");
  EXPECT_EQ(StripForTts("25度"), "二十五度");
  EXPECT_EQ(StripForTts("10个"), "十个");
  EXPECT_EQ(StripForTts("2024年"), "二零二四年");
  EXPECT_EQ(StripForTts("圆周率约为3.14"), "圆周率约为三点一四");
  EXPECT_EQ(StripForTts("05号"), "零五号");
}

}  // namespace
