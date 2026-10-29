#pragma once
#include "log_output.h"
#include <fstream>
class LogFileOutput:public LogOutput{
public:
    void Output(const std::string& log);
    bool Open(const std::string& file);
private:
    std::ofstream ofs_;
};

