#pragma once
#include <string>
#include <memory>
#include "log_format.h"
#include "log_output.h"
#include "xlog.h"
enum class XLog {
	DEBUG,
	INFO,
	ERROR,
	FATAL
};
class XLOG_API Logger
{
public:
	virtual void Write(
	XLog level,
	const std::string& log,
	const std::string& file,
	int line
	);
	void SetOutput(std::unique_ptr<LogOutput> o) {
		output_ = move(o);
	}
	void SetFormat(std::unique_ptr<LogFormat> f) {
		format_ = move(f);
	}
	void SetLevel(XLog l) {
		log_level = l;
	}
	~Logger() {
		//delete output_;
		//output_ = nullptr;
		//delete format_;
		//format_ = nullptr;
	}
private:
	//LogOutput *output_{nullptr};
	//LogFormat *format_{nullptr};
	std::unique_ptr<LogOutput> output_;
	std::unique_ptr<LogFormat> format_;
	XLog log_level = XLog::DEBUG;
};

