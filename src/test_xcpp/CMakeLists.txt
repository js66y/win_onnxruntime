# add_executable(test_xlog test_xlog.cpp)
# target_link_libraries(test_xlog PRIVATE xlog)
add_executable(test_llm test_llm.cpp)
target_link_libraries(test_llm PRIVATE llm)
