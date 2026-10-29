cmake_minimum_required(VERSION 3.20)
project(xlog VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# bin 放 exe 与 dll, lib 放导入库; 用生成表达式避免多配置生成器追加 Debug/Release
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_SOURCE_DIR}/bin>)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY $<1:${CMAKE_SOURCE_DIR}/lib>)

if(MSVC)
  add_compile_options(/utf-8)
endif()

add_subdirectory(src)
