#pragma once
#include "log_output.h"
class LogConsolOutput:public LogOutput
{
public:
    void Output(const std::string& log);
};

