#include "log_fac.h"
#include "xlog_format.h"
#include "log_consol_output.h"
#include "log_file_output.h"
#include "xconfig.h"
#include "xml_format.h"
#include "log_file_thread_output.h"
#define LOGFILE "log.txt"
#define LOGTYPE "console"
#define LOGLEVEL "debug"
#define LOGFMT "string"
#include <iostream>
using namespace std;

LogFac& LogFac::Instance() {
	static LogFac fac;
	return fac;
}

void LogFac::Init(const std::string& con_file) {
	XConfig conf;
	bool re = conf.Read(con_file);
	string log_type = LOGTYPE;
	string log_file = LOGFILE;
	string log_level = LOGLEVEL;
	string log_fmt = LOGFMT;
	if (re) {
		log_file = conf.Get("log_file");
		log_level = conf.Get("log_level");
		log_type = conf.Get("log_type");
		log_fmt = conf.Get("log_fmt");
	}
	if (log_type == "file")
	{
		if (log_file.empty())
		{
			log_file = LOGFILE;
		}
		auto fout = make_unique<LogFileThreadOutput>();
		if (!fout->Open(log_file))
		{
			cerr<<"open file fialed "<<log_file<<endl;
		}
		logger_.SetOutput(move(fout));
		if (log_level == "info") {
			logger_.SetLevel(XLog::INFO);
		}
		else if (log_level == "error") {
			logger_.SetLevel(XLog::ERROR);
		}
		else if (log_level == "fatal") {
			logger_.SetLevel(XLog::FATAL);
		}
	}
	else {
		logger_.SetOutput(make_unique<LogConsolOutput>());
	}
	if (log_fmt == "xml"){
		logger_.SetFormat(make_unique<XmlFormat>());
	}
	else {
		logger_.SetFormat(make_unique<XLogFormat>());
	}
}