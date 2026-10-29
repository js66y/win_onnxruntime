#include "xconfig.h"
#include <fstream>
using namespace std;
bool XConfig::Read(const std::string& file) {
	ifstream ifs(file);
	if(!ifs.is_open())return false;
	string line;
	while (getline(ifs, line)) {
		string k,v;
		if (!line.empty()) {
			auto p = line.find("=");
			if(p <= 0) continue;
			k = line.substr(0, p);
			v = line.substr(p+1);
			conf_[k] = v;
		}
	}
	return true;
}

const std::string& XConfig::Get(const std::string & key)
{
	static const std::string kEmpty;
	auto c=conf_.find(key);
	if (c==conf_.end()) return kEmpty;
	return c->second;
}