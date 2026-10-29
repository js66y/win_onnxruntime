#pragma once
#include <string>
// 日志格式化
class LogFormat {
public:
	virtual std::string Format(
			const std::string& level,
			const std::string& log, 
			const std::string& file,
			int line
	) = 0;
};