add_library(llm STATIC llm_api.cpp)

target_include_directories(llm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 真正发请求需要 curl + nlohmann/json; 不开这个宏时全部走 stub 分支
option(LLM_USE_HTTP_CLIENT "启用 curl HTTP 客户端" ON)

if(LLM_USE_HTTP_CLIENT)
  find_package(nlohmann_json CONFIG REQUIRED)
  find_package(CURL REQUIRED)
  target_compile_definitions(llm PUBLIC USE_HTTP_CLIENT)
  target_link_libraries(llm PRIVATE nlohmann_json::nlohmann_json CURL::libcurl)
endif()
