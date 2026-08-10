#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace echo::dialog {

// 流式切句: LLM 增量输出 -> 完整句子(供逐句 TTS)。
// - 句末标点(。!?;…等)且长度达到下限时切出
// - 积压过长时降级用逗号级标点切, 避免长句迟迟不发音
// - 首句更激进: 逗号也切且下限更低, 让 TTS 尽快开始出声(首响应延迟关键)
// - 全 UTF-8 安全: 按码点扫描, 不会从多字节字符中间切断
class SentenceSplitter {
 public:
  // min_bytes: 句子最短字节数(默认 ~4 个汉字), 避免"好。"这种碎句
  // soft_max_bytes: 积压超过该值后, 逗号级标点也允许切
  // first_min_bytes: 首句下限(默认 ~2 个汉字 + 标点即可切)
  explicit SentenceSplitter(size_t min_bytes = 12,
                            size_t soft_max_bytes = 90,
                            size_t first_min_bytes = 6)  // ~2 汉字, "好的," 即可切
      : min_bytes_(min_bytes),
        soft_max_bytes_(soft_max_bytes),
        first_min_bytes_(first_min_bytes) {}

  // 喂入增量文本, 返回 0..n 条切出的完整句子
  [[nodiscard]] std::vector<std::string> Feed(std::string_view delta) {
    buffer_ += delta;
    std::vector<std::string> sentences;

    size_t start = 0;  // 当前未切出部分的起点
    size_t pos = 0;
    while (pos < buffer_.size()) {
      const size_t char_len = Utf8CharLen(buffer_[pos]);
      if (pos + char_len > buffer_.size()) break;  // 末尾字符不完整, 等下批

      const std::string_view ch(buffer_.data() + pos, char_len);
      const size_t sentence_len = pos + char_len - start;
      const bool cut =
          first_cut_done_
              ? (IsTerminal(ch) && sentence_len >= min_bytes_) ||
                    (IsComma(ch) && sentence_len >= soft_max_bytes_)
              // 首句: 逗号也算, 下限更低, 越快出声越好
              : (IsTerminal(ch) || IsComma(ch)) &&
                    sentence_len >= first_min_bytes_;
      if (cut) {
        sentences.emplace_back(buffer_.substr(start, sentence_len));
        start = pos + char_len;
        first_cut_done_ = true;
      }
      pos += char_len;
    }

    buffer_.erase(0, start);
    return sentences;
  }

  // 生成结束: 取出缓冲中剩余的尾句(可能为空)
  [[nodiscard]] std::string Flush() {
    std::string tail = std::move(buffer_);
    buffer_.clear();
    return tail;
  }

 private:
  static size_t Utf8CharLen(char lead) {
    const auto byte = static_cast<unsigned char>(lead);
    if (byte < 0x80) return 1;
    if ((byte >> 5) == 0b110) return 2;
    if ((byte >> 4) == 0b1110) return 3;
    if ((byte >> 3) == 0b11110) return 4;
    return 1;  // 非法前导字节, 按单字节跳过
  }

  static bool IsTerminal(std::string_view ch) {
    constexpr std::string_view kTerminals[] = {
        "。", "!", "！", "?", "？", ";", "；", "…", "\n", ":", "：",
    };
    for (const auto terminal : kTerminals) {
      if (ch == terminal) return true;
    }
    return false;
  }

  static bool IsComma(std::string_view ch) {
    constexpr std::string_view kCommas[] = {",", "，", "、"};
    for (const auto comma : kCommas) {
      if (ch == comma) return true;
    }
    return false;
  }

  const size_t min_bytes_;
  const size_t soft_max_bytes_;
  const size_t first_min_bytes_;
  bool first_cut_done_ = false;
  std::string buffer_;
};

namespace detail {

inline bool IsAsciiDigit(char c) { return c >= '0' && c <= '9'; }

// 数字串转中文读法(melo-tts 词典没有阿拉伯数字, 不转换会被跳过不读):
// 两位数按数值读("25"->"二十五"), 单个数字/前导零/三位以上逐位读
// ("9"->"九", "2024"->"二零二四", 对年份/编号更自然)
inline void AppendChineseNumber(std::string_view digits, std::string& out) {
  constexpr std::string_view kDigits[] = {"零", "一", "二", "三", "四",
                                          "五", "六", "七", "八", "九"};
  const auto digit = [&](char c) { return kDigits[c - '0']; };

  if (digits.size() == 2 && digits[0] != '0') {
    if (digits[0] != '1') out += digit(digits[0]);
    out += "十";
    if (digits[1] != '0') out += digit(digits[1]);
    return;
  }
  for (const char c : digits) out += digit(c);
}

}  // namespace detail

// 送 TTS 前的文本清理:
// - 去掉 markdown 记号(朗读时不该念出来)
// - 阿拉伯数字转中文读法, 小数部分逐位("3.14"->"三点一四")
[[nodiscard]] inline std::string StripForTts(std::string_view text) {
  std::string out;
  out.reserve(text.size() * 2);

  size_t i = 0;
  while (i < text.size()) {
    const char c = text[i];
    // markdown + melo-tts 词典里没有的引号/括号(日志里会出现 Ignore OOV)
    if (c == '*' || c == '#' || c == '`' || c == '>' || c == '_') {
      ++i;
      continue;
    }
    // UTF-8 多字节标点: “ ” ‘ ’ （ ）
    if ((static_cast<unsigned char>(c) & 0xE0) == 0xE0 && i + 2 < text.size()) {
      const auto b1 = static_cast<unsigned char>(text[i + 1]);
      const auto b2 = static_cast<unsigned char>(text[i + 2]);
      const bool curly_quote =
          (static_cast<unsigned char>(c) == 0xE2 && b1 == 0x80 &&
           (b2 == 0x9C || b2 == 0x9D || b2 == 0x98 || b2 == 0x99));
      const bool fullwidth_paren =
          (static_cast<unsigned char>(c) == 0xEF && b1 == 0xBC &&
           (b2 == 0x88 || b2 == 0x89));
      if (curly_quote || fullwidth_paren) {
        i += 3;
        continue;
      }
    }
    if (detail::IsAsciiDigit(c)) {
      size_t end = i;
      while (end < text.size() && detail::IsAsciiDigit(text[end])) ++end;
      detail::AppendChineseNumber(text.substr(i, end - i), out);
      if (end + 1 < text.size() && text[end] == '.' &&
          detail::IsAsciiDigit(text[end + 1])) {
        out += "点";
        ++end;
        while (end < text.size() && detail::IsAsciiDigit(text[end])) {
          detail::AppendChineseNumber(text.substr(end, 1), out);
          ++end;
        }
      }
      i = end;
      continue;
    }
    out += c;
    ++i;
  }
  return out;
}

}  // namespace echo::dialog
