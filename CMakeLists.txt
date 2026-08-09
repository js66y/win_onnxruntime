cmake_minimum_required(VERSION 3.26)
project(echo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 所有可执行文件统一输出到 build/bin/<Config>/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 预编译第三方包所在目录(onnxruntime-genai / sherpa-onnx / 模型)
set(ECHO_THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

if(MSVC)
  add_compile_options(/utf-8)
endif()

include(FetchContent)

# 依赖缓存放 third_party/_deps(已被 .gitignore 覆盖),
# 这样 build_echo.bat clean 全量重建也不用重新下载
set(FETCHCONTENT_BASE_DIR "${ECHO_THIRD_PARTY_DIR}/_deps")

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
)

# Boost: 只启用 beast/asio(及其传递依赖), 头文件为主, 无需编译整套 Boost。
# 压缩包预先下载到 third_party/(GitHub 直连过慢), 原始地址:
#   https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.xz
set(BOOST_INCLUDE_LIBRARIES beast asio)
set(BOOST_ENABLE_CMAKE ON)
FetchContent_Declare(
  boost
  URL "${ECHO_THIRD_PARTY_DIR}/boost-1.87.0-cmake.tar.xz"
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(nlohmann_json boost)

add_subdirectory(src)

option(ECHO_BUILD_TESTS "Build unit/integration tests" ON)
if(ECHO_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
