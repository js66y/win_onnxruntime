# M0: 官方示例搬进本仓库做联调
# - model_chat: 依赖 third_party/onnxruntime-genai (include + lib)
# - sherpa_onnx_microphone: 依赖内部 C++ API, 通过编译 vendored sherpa-onnx 源码链接

include(FetchContent)

# ---------------------------------------------------------------------------
# 1) onnxruntime-genai: model_chat
# ---------------------------------------------------------------------------
set(ECHO_GENAI_ROOT "${ECHO_THIRD_PARTY_DIR}/onnxruntime-genai" CACHE PATH
  "Root of extracted onnxruntime-genai Windows package (contain include/ and lib/)")

set(_echo_genai_ok TRUE)
if(NOT EXISTS "${ECHO_GENAI_ROOT}/include/ort_genai.h")
  message(WARNING
    "Skip model_chat: missing ${ECHO_GENAI_ROOT}/include/ort_genai.h\n"
    "  Download win-x64(-dml) zip from https://github.com/microsoft/onnxruntime-genai/releases\n"
    "  Extract into third_party/onnxruntime-genai so that include/ and lib/ exist.")
  set(_echo_genai_ok FALSE)
endif()

if(_echo_genai_ok)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
  )
  FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.6.1
  )
  FetchContent_MakeAvailable(nlohmann_json CLI11)

  add_executable(model_chat
    model_chat.cpp
    common.cpp
  )

  target_include_directories(model_chat PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ECHO_GENAI_ROOT}/include
  )

  target_link_directories(model_chat PRIVATE ${ECHO_GENAI_ROOT}/lib)
  target_link_libraries(model_chat PRIVATE
    onnxruntime-genai
    onnxruntime
    nlohmann_json::nlohmann_json
    CLI11::CLI11
  )

  # 运行时把 DLL 拷到 exe 旁
  file(GLOB _echo_genai_runtime_files
    "${ECHO_GENAI_ROOT}/lib/*.dll"
    "${ECHO_GENAI_ROOT}/bin/*.dll"
  )
  foreach(_dll IN LISTS _echo_genai_runtime_files)
    add_custom_command(TARGET model_chat POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_dll}" $<TARGET_FILE_DIR:model_chat>
    )
  endforeach()

  message(STATUS "Demo target enabled: model_chat")
endif()

# ---------------------------------------------------------------------------
# 2) sherpa-onnx: microphone (内部 csrc API)
# ---------------------------------------------------------------------------
# 默认关闭: 编译整个 sherpa-onnx 源码耗时很长, M0 用 third_party 预编译工具验证即可
option(ECHO_BUILD_SHERPA_FROM_SOURCE
  "Build vendored sherpa-onnx-master to link sherpa_onnx_microphone" OFF)

set(ECHO_SHERPA_SRC "${CMAKE_SOURCE_DIR}/sherpa-onnx-master")
if(NOT EXISTS "${ECHO_SHERPA_SRC}/CMakeLists.txt")
  message(WARNING "Skip sherpa_onnx_microphone: missing ${ECHO_SHERPA_SRC}")
elseif(NOT ECHO_BUILD_SHERPA_FROM_SOURCE)
  message(STATUS "Skip sherpa_onnx_microphone: ECHO_BUILD_SHERPA_FROM_SOURCE=OFF")
else()
  # 与本工程 MSVC /MD 对齐; 关掉用不到的二进制/语言绑定以缩短编译
  set(SHERPA_ONNX_ENABLE_PYTHON OFF CACHE BOOL "" FORCE)
  set(SHERPA_ONNX_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
  set(SHERPA_ONNX_ENABLE_CHECK OFF CACHE BOOL "" FORCE)
  set(SHERPA_ONNX_ENABLE_WEBSOCKET OFF CACHE BOOL "" FORCE)
  set(SHERPA_ONNX_ENABLE_TTS OFF CACHE BOOL "" FORCE)
  set(SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION OFF CACHE BOOL "" FORCE)
  set(SHERPA_ONNX_ENABLE_BINARY OFF CACHE BOOL "" FORCE)
  set(SHERPA_ONNX_BUILD_C_API_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SHERPA_ONNX_ENABLE_PORTAUDIO ON CACHE BOOL "" FORCE)
  set(SHERPA_ONNX_USE_STATIC_CRT OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

  add_subdirectory(${ECHO_SHERPA_SRC} ${CMAKE_BINARY_DIR}/_deps/sherpa-onnx)

  add_executable(sherpa_onnx_microphone
    sherpa_onnx_microphone.cc
    ${ECHO_SHERPA_SRC}/sherpa-onnx/csrc/microphone.cc
  )

  target_include_directories(sherpa_onnx_microphone PRIVATE
    ${ECHO_SHERPA_SRC}
    ${ECHO_SHERPA_SRC}/sherpa-onnx
  )

  target_link_libraries(sherpa_onnx_microphone PRIVATE
    portaudio_static
    sherpa-onnx-core
  )

  if(MSVC)
    # 官方示例较吵, 不影响功能
    target_compile_options(sherpa_onnx_microphone PRIVATE /W3)
  endif()

  message(STATUS "Demo target enabled: sherpa_onnx_microphone (builds sherpa-onnx from source)")
endif()
