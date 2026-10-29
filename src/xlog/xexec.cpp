#include "xexec.h"
#include <iostream>
#include <string>
#include <functional>
#include <cstdio>
using namespace std;
#ifdef _WIN32
#else
#define _popen popen
#define _pclose pclose
#endif
bool XExec::Start(const char* cmd, function<void(const string& s)> func)
{
	cout<<"Start Cmd:"<<endl;
	string tmp = cmd;
	tmp += " 2>&1";
	auto fp = _popen(tmp.c_str(), "r");
	if (!fp) return false;
	running_ = true;
	fut_ = async([fp, this, func]{
		string tmp;
		char c = 0;
		while (c = fgetc(fp)) {  //直到\0
			if(c==EOF)break;
			if (c == '\n' || c=='\r') {
				//cout<<tmp<<endl;
				if(tmp.empty()) continue;
				{
					if(func)func(tmp);
					else {
						lock_guard<mutex> lock(mux_);
						outs_.push(tmp);
					}
				}
				tmp = "";
				continue;
			}
			tmp += c;
		}
		_pclose(fp);
		running_ = false;
		return true;
	});
	return true;
}


bool XExec::GetOutput(string& out) {
	lock_guard<mutex> lock(mux_);
	 if(outs_.empty())return false;
	 out = move(outs_.front());
	 outs_.pop();
	 return true;
}