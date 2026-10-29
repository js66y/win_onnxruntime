#include "logger.h"
#include <iostream>
using namespace std;

void Logger::Write(
	XLog level,
	const std::string& log,
	const std::string& file,
	int line
) {
	if (level < log_level) return;
	std::string levelstr = "debug";
	switch (level) {
		case XLog::INFO:
			levelstr = "info";
			break;
		case XLog::ERROR:
			levelstr = "error";
			break;
		case XLog::FATAL:
			levelstr = "fatal";
			break;
		default:
			break;
	}
	auto str = format_->Format(
		levelstr,
		log, file, line
	);
	output_->Output(str);
	//cout<<"make output."<<endl;
}