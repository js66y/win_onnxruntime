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
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(src)

option(ECHO_BUILD_TESTS "Build unit/integration tests" ON)
if(ECHO_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
