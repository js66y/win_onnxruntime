# ---------------------------------------------------------------------------
# echo_core: 可复用核心库(common + engine + server + storage)
# ---------------------------------------------------------------------------
set(ECHO_GENAI_ROOT "${ECHO_THIRD_PARTY_DIR}/onnxruntime-genai")
set(ECHO_SHERPA_ROOT "${ECHO_THIRD_PARTY_DIR}/sherpa-onnx")
set(ECHO_SQLITE_DIR
  "${ECHO_THIRD_PARTY_DIR}/sqlite/sqlite-amalgamation-3490100")

add_library(sqlite3 STATIC "${ECHO_SQLITE_DIR}/sqlite3.c")
target_include_directories(sqlite3 PUBLIC "${ECHO_SQLITE_DIR}")
target_compile_definitions(sqlite3 PUBLIC SQLITE_THREADSAFE=1)
if(MSVC)
  target_compile_options(sqlite3 PRIVATE /W0)
endif()

add_library(echo_core STATIC
  common/config.cpp
  common/console.cpp
  dialog/tools.cpp
  engine/audio_io.cpp
  engine/asr/sherpa_asr.cpp
  engine/llm/genai_llm.cpp
  engine/tts/sherpa_tts.cpp
  engine/vad/silero_vad.cpp
  server/chat_service.cpp
  server/http_server.cpp
  server/speech_service.cpp
  server/ws_session.cpp
  storage/session_store.cpp
)

target_include_directories(echo_core PUBLIC
  ${CMAKE_SOURCE_DIR}/src
)
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
  sqlite3
)

target_compile_definitions(echo_core PUBLIC
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  SHERPA_ONNX_BUILD_SHARED_LIBS
  _WIN32_WINNT=0x0A00
)

if(MSVC)
  target_compile_options(echo_core PRIVATE /W4)
endif()

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

add_executable(echo main.cpp)
target_link_libraries(echo PRIVATE echo_core shell32)
if(MSVC)
  target_compile_options(echo PRIVATE /W4)
endif()
echo_copy_runtime_dlls(echo)

option(ECHO_BUILD_SDK_DEMOS
  "Build src/demos (model_chat + sherpa_onnx_microphone)" ON)
if(ECHO_BUILD_SDK_DEMOS)
  add_subdirectory(demos)
endif()
