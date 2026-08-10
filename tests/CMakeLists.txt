include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.16.0
)
# 与主工程一致使用 /MD 运行时
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(echo_tests
  test_config.cpp
  test_engines.cpp
  test_sentence_splitter.cpp
  test_tools.cpp
  test_session_store.cpp
)

target_link_libraries(echo_tests PRIVATE
  echo_core
  GTest::gtest_main
)

# 测试 exe 与 echo 同目录输出(CMAKE_RUNTIME_OUTPUT_DIRECTORY),
# 运行时 DLL 由 echo 目标负责拷贝, 这里再拷一次保证单独构建也能跑
echo_copy_runtime_dlls(echo_tests)

add_test(NAME echo_tests COMMAND echo_tests)
