#pragma once
#include <string>
//级别，初始化
enum class LogType {
	Console,
	File
};
void LogInit(LogType logtype, std::string format = "{time}{content}{file}:{line}", std::string para = "");
void WriteLog(const std::string& level, const std::string& msg);
#define LOGDEBUG(s) WriteLog("DEBUG", s)
#define LOGINFO(s)  WriteLog("INFO",  s)
#define LOGERROR(s) WriteLog("ERROR", s)
#define LOGFATAL(s) WriteLog("FATAL", s)
