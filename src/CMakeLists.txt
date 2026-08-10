# ---------------------------------------------------------------------------
# echo_core: 可复用核心库(common + engine), 主程序与测试共同链接
# ---------------------------------------------------------------------------
set(ECHO_GENAI_ROOT "${ECHO_THIRD_PARTY_DIR}/onnxruntime-genai")
set(ECHO_SHERPA_ROOT "${ECHO_THIRD_PARTY_DIR}/sherpa-onnx")

add_library(echo_core STATIC
  common/config.cpp
  common/console.cpp
  engine/audio_io.cpp
  engine/asr/sherpa_asr.cpp
  engine/llm/genai_llm.cpp
  engine/tts/sherpa_tts.cpp
  engine/vad/silero_vad.cpp
  server/chat_service.cpp
  server/http_server.cpp
  server/speech_service.cpp
  server/ws_session.cpp
)

target_include_directories(echo_core PUBLIC
  ${CMAKE_SOURCE_DIR}/src
)
# SYSTEM: 第三方头文件的警告(如 sherpa 的 C4305)不按 /W4 报
target_include_directories(echo_core SYSTEM PUBLIC
  ${ECHO_GENAI_ROOT}/include
  ${ECHO_SHERPA_ROOT}/include
)

target_link_directories(echo_core PUBLIC
  ${ECHO_GENAI_ROOT}/lib
  ${ECHO_SHERPA_ROOT}/lib
)

target_link_libraries(echo_core PUBLIC
  onnxruntime-genai
  sherpa-onnx-cxx-api
  sherpa-onnx-c-api
  nlohmann_json::nlohmann_json
  Boost::beast
  Boost::asio
)

# SHERPA_ONNX_BUILD_SHARED_LIBS: 消费预编译 DLL 时启用 dllimport
# _WIN32_WINNT=0x0A00: Asio 需要明确目标 Windows 版本(Win10+)
target_compile_definitions(echo_core PUBLIC
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  SHERPA_ONNX_BUILD_SHARED_LIBS
  _WIN32_WINNT=0x0A00
)

if(MSVC)
  target_compile_options(echo_core PRIVATE /W4)
endif()

# ---------------------------------------------------------------------------
# 运行时 DLL: genai 全部 + sherpa 的 c/cxx api。
# 注意: 两个包都带 onnxruntime.dll, 统一使用 genai 的(1.28, 较新;
# ORT C API 向后兼容, sherpa 由 1.27 头文件编译可以跑在 1.28 上)。
# ---------------------------------------------------------------------------
file(GLOB _echo_runtime_dlls "${ECHO_GENAI_ROOT}/lib/*.dll")
list(APPEND _echo_runtime_dlls
  "${ECHO_SHERPA_ROOT}/lib/sherpa-onnx-c-api.dll"
  "${ECHO_SHERPA_ROOT}/lib/sherpa-onnx-cxx-api.dll"
)

function(echo_copy_runtime_dlls target)
  foreach(_dll IN LISTS _echo_runtime_dlls)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_dll}" $<TARGET_FILE_DIR:${target}>
    )
  endforeach()
endfunction()

# ---------------------------------------------------------------------------
# echo 主程序
# ---------------------------------------------------------------------------
add_executable(echo main.cpp)
target_link_libraries(echo PRIVATE echo_core shell32)  # shell32: 启动时自动打开浏览器
if(MSVC)
  target_compile_options(echo PRIVATE /W4)
endif()
echo_copy_runtime_dlls(echo)

# ---------------------------------------------------------------------------
# 官方 SDK 示例(各自独立 main, 不链进 echo)
# ---------------------------------------------------------------------------
option(ECHO_BUILD_SDK_DEMOS
  "Build src/demos (model_chat + sherpa_onnx_microphone)" ON)
if(ECHO_BUILD_SDK_DEMOS)
  add_subdirectory(demos)
endif()
