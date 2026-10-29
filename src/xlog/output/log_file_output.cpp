#include "log_file_output.h"

bool LogFileOutput::Open(const std::string& file) {
	ofs_.open(file, std::ios::app);
	if (ofs_.is_open()) return true;
	return false;
}
void LogFileOutput::Output(const std::string& log) {
	ofs_ << log << "\n";
}