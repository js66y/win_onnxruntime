# ---------------------------------------------------------------------------
# echo_core: 可复用核心库(common + engine + server + storage)
# ---------------------------------------------------------------------------
set(ECHO_GENAI_ROOT "${ECHO_THIRD_PARTY_DIR}/onnxruntime-genai")
set(ECHO_SHERPA_ROOT "${ECHO_THIRD_PARTY_DIR}/sherpa-onnx")

# --- sqlite3: 优先用本地 amalgamation, 否则退回到系统 sqlite3 ---
file(GLOB _echo_sqlite_amalgamation_dirs
  "${ECHO_THIRD_PARTY_DIR}/sqlite/sqlite-amalgamation-*")
set(ECHO_SQLITE_DIR "")
if(_echo_sqlite_amalgamation_dirs)
  list(GET _echo_sqlite_amalgamation_dirs 0 ECHO_SQLITE_DIR)
endif()
if(ECHO_SQLITE_DIR AND EXISTS "${ECHO_SQLITE_DIR}/sqlite3.c")
  message(STATUS "sqlite3: 使用本地 amalgamation ${ECHO_SQLITE_DIR}")
  add_library(sqlite3 STATIC "${ECHO_SQLITE_DIR}/sqlite3.c")
  target_include_directories(sqlite3 PUBLIC "${ECHO_SQLITE_DIR}")
  target_compile_definitions(sqlite3 PUBLIC SQLITE_THREADSAFE=1)
  if(MSVC)
    target_compile_options(sqlite3 PRIVATE /W0)
  else()
    target_compile_options(sqlite3 PRIVATE -w)
  endif()
  set(ECHO_SQLITE_TARGET sqlite3)
else()
  find_package(SQLite3 REQUIRED)
  message(STATUS "sqlite3: 使用系统 SQLite3 ${SQLite3_VERSION}")
  set(ECHO_SQLITE_TARGET SQLite3::SQLite3)
endif()

# --- ort_genai.h 幂等补丁 (仅 non-Windows) ---
# 官方 header 中 OgaGenerator::GetInput/GetOutput/GetLogits 与
# OgaTokenizer::EncodeBatch 都是返回 std::unique_ptr<OgaTensor> 的内联函数,
# 但 OgaTensor 的完整定义排在这些函数之后 ── Apple libc++ / 较新 libstdc++ 的
# ~unique_ptr 上 static_assert(sizeof(_Tp) >= 0) 会立刻报 incomplete type。
# MSVC 的两阶段查找较宽松, 不受影响。
# 这里做两件事(幂等, 通过标记字符串判断是否已 patch):
#   1) 移除 EncodeBatch (项目未使用, 且是首个触发点)。
#   2) 把 OgaTensor 定义整体上移到 OgaGenerator 之前。
if(NOT WIN32)
  set(_echo_genai_header "${ECHO_GENAI_ROOT}/include/ort_genai.h")
  if(EXISTS "${_echo_genai_header}")
    file(READ "${_echo_genai_header}" _echo_genai_content)
    # marker 必须选一个 patch 后一定出现、原 header 里没有的字面字符串
    if(NOT _echo_genai_content MATCHES "echo: EncodeBatch")
      set(_echo_encode_batch [==[  std::unique_ptr<OgaTensor> EncodeBatch(const char** strings, size_t count) const {
    OgaTensor* out;
    OgaCheckResult(OgaTokenizerEncodeBatch(this, strings, count, &out));
    return std::unique_ptr<OgaTensor>(out);
  }]==])
      set(_echo_encode_batch_stub [==[  // echo: EncodeBatch 移除 (OgaTensor 未完整时会破坏 Apple libc++)]==])
      set(_echo_tensor_orig [==[struct OgaTensor : OgaAbstract {
#if OGA_USE_SPAN
  template <typename T>
  static std::unique_ptr<OgaTensor> Create(T* data, std::span<const int64_t> shape) {
    OgaTensor* p;
    OgaCheckResult(OgaCreateTensorFromBuffer(data, shape.data(), shape.size(), OgaTypeToElementType<T>, &p));
    return std::unique_ptr<OgaTensor>(p);
  }

  static std::unique_ptr<OgaTensor> Create(void* data, std::span<const int64_t> shape, OgaElementType type) {
    OgaTensor* p;
    OgaCheckResult(OgaCreateTensorFromBuffer(data, shape.data(), shape.size(), type, &p));
    return std::unique_ptr<OgaTensor>(p);
  }
#endif

  static std::unique_ptr<OgaTensor> Create(void* data, const int64_t* shape_dims, size_t shape_dims_count, OgaElementType element_type) {
    OgaTensor* p;
    OgaCheckResult(OgaCreateTensorFromBuffer(data, shape_dims, shape_dims_count, element_type, &p));
    return std::unique_ptr<OgaTensor>(p);
  }

  OgaElementType Type() {
    OgaElementType type;
    OgaCheckResult(OgaTensorGetType(this, &type));
    return type;
  }

  std::vector<int64_t> Shape() {
    size_t size;
    OgaCheckResult(OgaTensorGetShapeRank(this, &size));
    std::vector<int64_t> shape(size);
    OgaCheckResult(OgaTensorGetShape(this, shape.data(), shape.size()));
    return shape;
  }

  void* Data() {
    void* data;
    OgaCheckResult(OgaTensorGetData(this, &data));
    return data;
  }

  static void operator delete(void* p) { OgaDestroyTensor(reinterpret_cast<OgaTensor*>(p)); }
};

struct OgaImages : OgaAbstract {]==])
      set(_echo_tensor_moved [==[// echo: OgaTensor 定义上移 (兼容 Apple libc++)
struct OgaTensor : OgaAbstract {
#if OGA_USE_SPAN
  template <typename T>
  static std::unique_ptr<OgaTensor> Create(T* data, std::span<const int64_t> shape) {
    OgaTensor* p;
    OgaCheckResult(OgaCreateTensorFromBuffer(data, shape.data(), shape.size(), OgaTypeToElementType<T>, &p));
    return std::unique_ptr<OgaTensor>(p);
  }

  static std::unique_ptr<OgaTensor> Create(void* data, std::span<const int64_t> shape, OgaElementType type) {
    OgaTensor* p;
    OgaCheckResult(OgaCreateTensorFromBuffer(data, shape.data(), shape.size(), type, &p));
    return std::unique_ptr<OgaTensor>(p);
  }
#endif

  static std::unique_ptr<OgaTensor> Create(void* data, const int64_t* shape_dims, size_t shape_dims_count, OgaElementType element_type) {
    OgaTensor* p;
    OgaCheckResult(OgaCreateTensorFromBuffer(data, shape_dims, shape_dims_count, element_type, &p));
    return std::unique_ptr<OgaTensor>(p);
  }

  OgaElementType Type() {
    OgaElementType type;
    OgaCheckResult(OgaTensorGetType(this, &type));
    return type;
  }

  std::vector<int64_t> Shape() {
    size_t size;
    OgaCheckResult(OgaTensorGetShapeRank(this, &size));
    std::vector<int64_t> shape(size);
    OgaCheckResult(OgaTensorGetShape(this, shape.data(), shape.size()));
    return shape;
  }

  void* Data() {
    void* data;
    OgaCheckResult(OgaTensorGetData(this, &data));
    return data;
  }

  static void operator delete(void* p) { OgaDestroyTensor(reinterpret_cast<OgaTensor*>(p)); }
};

struct OgaGenerator : OgaAbstract {]==])
      string(REPLACE "${_echo_encode_batch}" "${_echo_encode_batch_stub}"
        _echo_genai_content "${_echo_genai_content}")
      string(REPLACE "${_echo_tensor_orig}"
        "// echo: OgaTensor 原始定义已上移\n\nstruct OgaImages : OgaAbstract {"
        _echo_genai_content "${_echo_genai_content}")
      string(REPLACE "struct OgaGenerator : OgaAbstract {"
        "${_echo_tensor_moved}"
        _echo_genai_content "${_echo_genai_content}")
      file(WRITE "${_echo_genai_header}" "${_echo_genai_content}")
      message(STATUS "已修补 ort_genai.h (兼容 Apple libc++ / libstdc++)")
    endif()
  endif()
endif()

# --- 找 onnxruntime-genai / sherpa-onnx 的动态库(不同平台后缀不同) ---
find_library(ECHO_ORT_GENAI_LIB
  NAMES onnxruntime-genai
  PATHS "${ECHO_GENAI_ROOT}/lib"
  NO_DEFAULT_PATH
  REQUIRED)
find_library(ECHO_SHERPA_C_LIB
  NAMES sherpa-onnx-c-api
  PATHS "${ECHO_SHERPA_ROOT}/lib"
  NO_DEFAULT_PATH
  REQUIRED)
find_library(ECHO_SHERPA_CXX_LIB
  NAMES sherpa-onnx-cxx-api
  PATHS "${ECHO_SHERPA_ROOT}/lib"
  NO_DEFAULT_PATH
  REQUIRED)

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

target_link_libraries(echo_core PUBLIC
  ${ECHO_ORT_GENAI_LIB}
  ${ECHO_SHERPA_CXX_LIB}
  ${ECHO_SHERPA_C_LIB}
  nlohmann_json::nlohmann_json
  Boost::beast
  Boost::asio
  ${ECHO_SQLITE_TARGET}
)

if(WIN32)
  target_compile_definitions(echo_core PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
  )
endif()
target_compile_definitions(echo_core PUBLIC SHERPA_ONNX_BUILD_SHARED_LIBS)

if(MSVC)
  target_compile_options(echo_core PRIVATE /W4)
else()
  target_compile_options(echo_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# --- 运行时依赖库(dll/dylib/so): 拷到 exe 旁, 保证 rpath 找得到 ---
if(WIN32)
  file(GLOB _echo_runtime_libs
    "${ECHO_GENAI_ROOT}/lib/*.dll"
    "${ECHO_SHERPA_ROOT}/lib/*.dll"
  )
elseif(APPLE)
  file(GLOB _echo_runtime_libs
    "${ECHO_GENAI_ROOT}/lib/*.dylib"
    "${ECHO_SHERPA_ROOT}/lib/*.dylib"
  )
else()
  file(GLOB _echo_runtime_libs
    "${ECHO_GENAI_ROOT}/lib/*.so*"
    "${ECHO_SHERPA_ROOT}/lib/*.so*"
  )
endif()

function(echo_copy_runtime_dlls target)
  foreach(_dll IN LISTS _echo_runtime_libs)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_dll}" $<TARGET_FILE_DIR:${target}>
    )
  endforeach()
endfunction()

add_executable(echo main.cpp)
target_link_libraries(echo PRIVATE echo_core)
if(WIN32)
  target_link_libraries(echo PRIVATE shell32)
endif()
if(MSVC)
  target_compile_options(echo PRIVATE /W4)
else()
  target_compile_options(echo PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
echo_copy_runtime_dlls(echo)

# demos: 官方 sample, 目前只在 Windows 下验证过, 其他平台默认关闭
if(WIN32)
  set(_echo_demos_default ON)
else()
  set(_echo_demos_default OFF)
endif()
option(ECHO_BUILD_SDK_DEMOS
  "Build src/demos (model_chat + sherpa_onnx_microphone)"
  ${_echo_demos_default})
if(ECHO_BUILD_SDK_DEMOS)
  add_subdirectory(demos)
endif()
