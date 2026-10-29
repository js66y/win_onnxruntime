set(XLOG_SOURCES
  log_fac.cpp
  logger.cpp
  log_consol_output.cpp
  log_file_output.cpp
  log_file_thread_output.cpp
  xconfig.cpp
  xexec.cpp
  user_input.cpp
  xlog.cpp
  xlog_format.cpp
  xml_format.cpp
)

add_library(xlog SHARED ${XLOG_SOURCES})

# 编译 DLL 时必须定义 XLOG_EXPORTS, 否则 XLOG_API 会被当成 dllimport
target_compile_definitions(xlog PRIVATE XLOG_EXPORTS)

target_include_directories(xlog
  PUBLIC ${CMAKE_SOURCE_DIR}/include/xlog
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(Threads REQUIRED)
target_link_libraries(xlog PRIVATE Threads::Threads)

if(WIN32)
  # 统一为 xlog.dll，关掉前缀
  set_target_properties(xlog PROPERTIES PREFIX "")
endif()

