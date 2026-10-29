#pragma once
#include "log_format.h"
class XLogFormat :
    public LogFormat
{
public:
	std::string Format(
		const std::string& level,
		const std::string& log,
		const std::string& file,
		int line
	);
};

