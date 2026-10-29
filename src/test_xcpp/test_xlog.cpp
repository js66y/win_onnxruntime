// xlog 测试: 验证 DLL 导出、头文件与链接是否正常
#include "log_fac.h"

int main() {
	XLOGINIT();
	LOGDEBUG("xlog test: debug");
	LOGINFO("xlog test: info");
	LOGERROR("xlog test: error");
	LOGFATAL("xlog test: fatal");
	return 0;
}
