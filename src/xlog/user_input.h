#pragma once
#include "xlog.h"
#include <map>
#include <functional>
#include <string>
class XLOG_API UserInput
{
public:
	void Start(std::function<void()>init=[]{});
	void Stop(){is_exit_=true; };
	UserInput& Reg(std::string key, std::function<void(const std::string&)>func);
	UserInput& RegTask(std::string key, std::function<void()>func) {
		task_funcs_[key] = func;
		return *this;
	}
private:
	bool is_exit_ = false;
	std::map<std::string, std::function<void(const std::string&)>>key_funcs_;
	//std::function<void()> init_func_ = []{};
	std::map<std::string, std::function<void()>> task_funcs_;
};

