#pragma once
#include <map>
#include <string>
class XConfig
{
public:
	bool Read(const std::string& file);
	const std::string& Get(const std::string& key);
private:
	std::map<std::string,std::string>conf_;
};

