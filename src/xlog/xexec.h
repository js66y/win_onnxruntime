#pragma once+
#include "xlog.h"
#include <queue>
#include <string>
#include <future>
#include <mutex>
#include <functional>
class XLOG_API XExec
{
public:
	bool Start(const char* cmd, 
		std::function<void(const std::string& s)> func=nullptr);
	bool Running(){return running_;};
	bool GetOutput(std::string &out);
	bool Wait(){return fut_.get();};
private:
	bool running_ = false;
	std::queue<std::string> outs_;
	std::future<bool> fut_;
	std::mutex mux_;
};

