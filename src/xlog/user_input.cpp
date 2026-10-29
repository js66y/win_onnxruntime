#include "user_input.h"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
using namespace std;
static vector<string> Split(const string& s) {
	vector<string> vec;
	string tmp;
	istringstream is(s);
	while (getline(is, tmp, ' ')) {
		if (tmp.empty()) continue;
		vec.push_back(tmp);
	}
	return vec;
}
void UserInput::Start(std::function<void()>init) {
	cout<<"UserInput::Start()" << endl;
	string input;
	while(!is_exit_){
		cout << "\n>>"<<flush;
		getline(cin, input);
		if(input.empty())continue;
		if(input=="exit")break;
		auto vec = Split(input);
		if(vec.empty())continue;
		string type = vec[0];
		init();

		for (int i = 1; i < vec.size(); i++) {
			auto &k=vec[i];
			auto fitr = key_funcs_.find(k);
			if (fitr!=key_funcs_.end()) {
				if (vec.size() - 1 > i) {
					auto &v = vec[i+1]; 
					if (key_funcs_.find(v) == key_funcs_.end())
					{
						cout<<k<<":"<<v<<endl;
						fitr->second(v);
						i++;
						continue;
					}
				}
				fitr->second("");
				cout << k << ":" << " " << endl;
				//cout<<k<<endl;
			}
		}

		auto task = task_funcs_.find(type);
		if (task == task_funcs_.end()) {
			cout<<type<<" not support!"<<endl;
		}else
			task->second();
	}
}

UserInput& UserInput::Reg(std::string key, std::function<void(const std::string&)>func) {
	key_funcs_[key] = func;
	return *this;
}