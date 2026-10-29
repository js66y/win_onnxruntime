#include "log_file_thread_output.h"
#include <iostream>
#include <thread>
#include <chrono>
using namespace std;

void LogFileThreadOutput::Output(const std::string& log)
{  // 添加到logs_
	lock_guard<mutex> lock(mux_);
	logs_.push(log);
	//cout<<"push in logs_"<<endl;
	//this_thread::sleep_for(200ms);
}

bool LogFileThreadOutput::Open(const std::string& file)
{  //启动write线程将log持续的写入file
	if (th_.joinable()) {
		return false;
	}else{
		th_ = thread(&LogFileThreadOutput::Write, this, file);
		//cout<<"make th_ "<<th_.joinable()<< endl;
		return true;
	}
}

void LogFileThreadOutput::Write(const std::string& file) 
{
	ofs_.open(file, std::ios::app);
	if (!ofs_.is_open()) return;
	while (true) {
		//cout<<"."<<endl;
		//lock_guard<mutex> lock(mux_);
		if (logs_.empty()) {
			if (is_exit_) break;
			continue;
		}
		else {
			lock_guard<mutex> lock(mux_);
			auto log = logs_.front();
			logs_.pop();
			ofs_ << log << "\n";
		}
		ofs_.flush();
	}
}

LogFileThreadOutput::~LogFileThreadOutput()
{
	is_exit_ = true;
	cout<<"exit"<<endl;
	if(th_.joinable())th_.join();
}
