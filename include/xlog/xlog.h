#pragma once
// 下列 ifdef 块是创建使从 DLL 导出更简单的
// 宏的标准方法。此 DLL 中的所有文件都是用命令行上定义的 XLOG_EXPORTS
// 符号编译的。在使用此 DLL 的
// 任何项目上不应定义此符号。这样，源文件中包含此文件的任何其他项目都会将
// XLOG_API 函数视为是从 DLL 导入的，而此 DLL 则将用此宏定义的
// 符号视为是被导出的。
#if defined(_WIN32)
	#if defined(XLOG_STATIC)  //静态
	#define XLOG_API
	#elif defined(XLOG_EXPORTS)  //动态导出
	#define XLOG_API __declspec(dllexport)
	#else
	#define XLOG_API __declspec(dllimport)  //动态导入
	#endif
#elif defined(__GNUC__)
	#define XLOG_API __attribute__((visibility("default")))
#else
	#define XLOG_API
#endif
// 此类是从 dll 导出的
class XLOG_API Cxlog {
public:
	Cxlog(void);
	// TODO: 在此处添加方法。
};
