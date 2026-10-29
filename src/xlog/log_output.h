#pragma once
#include <string>
class LogOutput
{
public:
	virtual ~LogOutput() = default;
	virtual void Output(const std::string& log)=0;
};

