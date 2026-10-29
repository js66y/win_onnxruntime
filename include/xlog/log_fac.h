#pragma once
#include <string>
#include "logger.h"
#include "xlog.h"
class XLOG_API LogFac
{
public:
	static LogFac& Instance();
	void Init(const std::string& con_file="log.conf");
	Logger& logger(){return logger_;}
private:
	LogFac(){}
	Logger logger_;
};
#define XLOGINIT() LogFac::Instance().Init()
#define XLOGOUT(l, s) LogFac::Instance().logger().Write(l,s,__FILE__, __LINE__)
#define LOGDEBUG(s) XLOGOUT(XLog::DEBUG,s)
#define LOGINFO(s) XLOGOUT(XLog::INFO,s)
#define LOGERROR(s) XLOGOUT(XLog::ERROR,s)
#define LOGFATAL(s) XLOGOUT(XLog::FATAL,s)