#pragma once
#include "log_file_output.h"
#include <thread>
#include <queue>
#include <mutex>
class LogFileThreadOutput :
    public LogFileOutput
{
public:
    void Output(const std::string& log);
    bool Open(const std::string& file);
    void Write(const std::string& file);
    ~LogFileThreadOutput();

private:
    std::ofstream ofs_;
    std::thread th_;  // 独立线程写入文件
    std::queue<std::string> logs_;  // 缓存
    bool is_exit_{false};  // 退出标识
    std::mutex mux_;  //互斥访问logs_  lock_guard<mutex> lock(mux_);
};