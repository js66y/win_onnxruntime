# echo 主程序 (M1: 命令行流式聊天)
set(ECHO_GENAI_ROOT "${ECHO_THIRD_PARTY_DIR}/onnxruntime-genai")

add_executable(echo
  main.cpp
  common/config.cpp
  common/console.cpp
  engine/llm/genai_llm.cpp
)

target_include_directories(echo PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${ECHO_GENAI_ROOT}/include
)

target_link_directories(echo PRIVATE ${ECHO_GENAI_ROOT}/lib)
target_link_libraries(echo PRIVATE
  onnxruntime-genai
  nlohmann_json::nlohmann_json
)

target_compile_definitions(echo PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
  target_compile_options(echo PRIVATE /W4)
endif()

# 运行时依赖的 DLL 拷到 exe 旁
file(GLOB _echo_genai_dlls "${ECHO_GENAI_ROOT}/lib/*.dll")
foreach(_dll IN LISTS _echo_genai_dlls)
  add_custom_command(TARGET echo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_dll}" $<TARGET_FILE_DIR:echo>
  )
endforeach()

# 官方 SDK 示例(各自独立 main, 不链进 echo)
option(ECHO_BUILD_SDK_DEMOS
  "Build src/demos (model_chat + sherpa_onnx_microphone)" ON)
if(ECHO_BUILD_SDK_DEMOS)
  add_subdirectory(demos)
endif()
