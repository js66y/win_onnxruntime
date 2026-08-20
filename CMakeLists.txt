cmake_minimum_required(VERSION 3.26)
project(echo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 所有可执行文件统一输出到 build/bin/[<Config>/]
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Mac/Linux 运行时依赖第三方 .dylib/.so, 让可执行文件优先在自己旁边找
if(APPLE)
  set(CMAKE_INSTALL_RPATH "@executable_path")
  set(CMAKE_BUILD_RPATH "@executable_path")
  set(CMAKE_MACOSX_RPATH ON)
elseif(UNIX)
  set(CMAKE_INSTALL_RPATH "\$ORIGIN")
  set(CMAKE_BUILD_RPATH "\$ORIGIN")
endif()
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

# 预编译第三方包所在目录(onnxruntime-genai / sherpa-onnx / 模型)
set(ECHO_THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

if(MSVC)
  add_compile_options(/utf-8)
endif()

include(FetchContent)

# 依赖缓存放 third_party/_deps(已被 .gitignore 覆盖),
# 这样 clean 全量重建也不用重新下载
set(FETCHCONTENT_BASE_DIR "${ECHO_THIRD_PARTY_DIR}/_deps")

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
)

# Boost: 只启用 beast/asio(及其传递依赖), 头文件为主, 无需编译整套 Boost。
# 优先用本地压缩包(GitHub 直连过慢), 缺失时回退到从 URL 下载。
set(BOOST_INCLUDE_LIBRARIES beast asio)
set(BOOST_ENABLE_CMAKE ON)
if(EXISTS "${ECHO_THIRD_PARTY_DIR}/boost-1.87.0-cmake.tar.xz")
  FetchContent_Declare(
    boost
    URL "${ECHO_THIRD_PARTY_DIR}/boost-1.87.0-cmake.tar.xz"
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
  )
else()
  FetchContent_Declare(
    boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
  )
endif()

FetchContent_MakeAvailable(nlohmann_json boost)

add_subdirectory(src)

option(ECHO_BUILD_TESTS "Build unit/integration tests" ON)
if(ECHO_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
