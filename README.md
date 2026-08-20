# Echo（回声）

全离线中文 AI 语音助手：浏览器说话 / 打字，本机完成 **VAD → ASR → LLM → TTS**，聊天内容不离开你的电脑。

基于 [onnxruntime-genai](https://github.com/microsoft/onnxruntime-genai) 与 [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx)，用现代 C++ 实现推理服务端，前端为原生 HTML/JS。

## 功能

- 文字聊天：流式打字机输出
- 语音对话：麦克风实时上行，VAD 切段 + SenseVoice 识别，逐句 MeloTTS 合成回放
- 可打断（barge-in）：助手播放时可插话停止
- 角色切换：回声 / 耐心老师 / 编程搭档（可在 `echo.json` 扩展）
- 会话持久化：SQLite 保存历史
- 本地工具：查时间、简易计算等

## 技术栈

| 模块 | 选型 |
|------|------|
| 语言 / 构建 | C++23 · CMake（Ninja / MSVC / Make） |
| 网络 | Boost.Asio + Beast（HTTP + WebSocket） |
| LLM | onnxruntime-genai · Qwen3-0.6B INT4（CPU） |
| ASR | sherpa-onnx · SenseVoice Small int8 |
| TTS | sherpa-onnx · MeloTTS 中英（VITS） |
| VAD | Silero VAD |
| 存储 | SQLite |

更完整的设计见 [DESIGN.md](DESIGN.md)。

## 环境要求

支持 **Windows / macOS / Linux**，仅需 CPU；平台需与所下载的 `onnxruntime-genai`、
`sherpa-onnx` 预编译包架构对应（x64 或 arm64）。

- Windows 10/11 x64：Visual Studio 2022/2026（含 C++ 桌面开发与 CMake）
- macOS 13+（Apple Silicon / Intel）：`xcode-select --install`，`brew install cmake ninja`
- Linux（Ubuntu 24.04 / Fedora 40+ 等）：`gcc-14`/`clang-18`+、`cmake`、`ninja`、`libsqlite3-dev`
- Git + [Git LFS](https://git-lfs.com/)（下载模型需要）
- 建议内存 ≥ 8 GB；首次加载模型会占用数 GB 磁盘

## 快速开始

### 1. 克隆仓库

```bash
git clone https://github.com/JS66y/echo-assistant.git
cd echo-assistant
```

### 2. 准备第三方库与模型

`third_party/` 与 `models/` **不入库**，需自行准备（体积较大）。

**LLM 与运行时（放入 `third_party/`）：**

根据本机平台下载对应架构的预编译包并解压：

```bash
mkdir -p third_party && cd third_party

# onnxruntime-genai (二选一)
# macOS arm64
curl -L -o genai.tgz https://github.com/microsoft/onnxruntime-genai/releases/download/v0.15.2/onnxruntime-genai-0.15.2-osx-arm64.tar.gz
# Linux x64
# curl -L -o genai.tgz https://github.com/microsoft/onnxruntime-genai/releases/download/v0.15.2/onnxruntime-genai-0.15.2-linux-x64.tar.gz
mkdir onnxruntime-genai && tar xzf genai.tgz -C onnxruntime-genai --strip-components=1 && rm genai.tgz

# sherpa-onnx (需与 onnxruntime 1.28 匹配)
curl -L -o sherpa.tar.bz2 https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.13.6/sherpa-onnx-v1.13.6-onnxruntime-1.28.0-osx-arm64-shared.tar.bz2
mkdir sherpa-onnx && tar xjf sherpa.tar.bz2 -C sherpa-onnx --strip-components=1 && rm sherpa.tar.bz2

cd ..
```

Windows 用户在同一 [release 页面](https://github.com/microsoft/onnxruntime-genai/releases)下载
`win-x64.zip` 与对应的 `sherpa-onnx-*-win-x64-shared.tar.bz2`，同样解压到
`third_party/onnxruntime-genai/` 与 `third_party/sherpa-onnx/`（保持 `include/` 与 `lib/` 结构）。

Windows 若需要静态链接 SQLite，把 [SQLite Amalgamation](https://www.sqlite.org/download.html) 放到
`third_party/sqlite/sqlite-amalgamation-<版本>/`；Mac/Linux 上 CMake 会直接找系统 SQLite3，无需此步。

Boost 会在 `cmake` configure 阶段自动下载 `boost-1.87.0-cmake.tar.xz`（也可
预先手动放到 `third_party/` 加速）。

**语音 / LLM 模型（放入 `models/` 与 `third_party/`）：**

```bash
mkdir -p models && cd models
# ASR: SenseVoice
git clone --depth 1 https://huggingface.co/csukuangfj/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17
# TTS: MeloTTS 中英
git clone --depth 1 https://huggingface.co/csukuangfj/vits-melo-tts-zh_en
# VAD: 从 sherpa-onnx release 或官方仓库获取 silero_vad.onnx 放到 models/silero_vad.onnx
cd ..

# LLM: Qwen3-0.6B INT4 CPU (示例路径, 见 echo.json 中 llm.model_dir)
# 放到 third_party/Qwen3-0.6B-ONNX-INT4-CPU/, 需包含 genai_config.json 与相关权重
```

### 3. 编译

**macOS / Linux（Ninja）：**

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

产物在 `build/bin/echo` 与 `build/bin/echo_tests`，第三方 `.dylib` / `.so`
会自动拷到同一目录，rpath 设置为 `@executable_path` / `$ORIGIN`。

**Windows：**

```powershell
.\build_echo.bat
# 全量重建: .\build_echo.bat clean
```

或者手动跑 CMake：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

产物在 `build\bin\Release\echo.exe`。

### 4. 运行

在仓库根目录运行（以便找到 `echo.json`、`web/`、`models/`）：

```bash
# macOS / Linux
./build/bin/echo --serve                              # Web 服务
./build/bin/echo                                      # 命令行文字聊天
./build/bin/echo --voice question.wav answer.wav     # wav 进 → wav 出

# Windows
.\build\bin\Release\echo.exe --serve
```

`--serve` 会启动 `http://127.0.0.1:8080/` 并尝试打开浏览器。按 `Ctrl+C` 退出。

## 配置

主配置文件为根目录 [`echo.json`](echo.json)，相对路径相对配置文件所在目录解析。

常用项：

- `llm.model_dir`：本地大模型目录
- `asr` / `tts` / `vad`：模型路径与线程数
- `server.host` / `port` / `web_root` / `db_path`
- `roles` / `active_role`：人设与默认角色

TTS 当前为 MeloTTS：

```json
"tts": {
  "model": "models/vits-melo-tts-zh_en/model.onnx",
  "lexicon": "models/vits-melo-tts-zh_en/lexicon.txt",
  "tokens": "models/vits-melo-tts-zh_en/tokens.txt",
  "dict_dir": "models/vits-melo-tts-zh_en/dict",
  "num_threads": 4,
  "speaker_id": 0,
  "speed": 1.0
}
```

## 目录结构

```
├── src/
│   ├── common/     配置、日志、错误处理
│   ├── engine/     VAD / ASR / TTS / LLM 封装
│   ├── dialog/     切句、工具调用
│   ├── server/     HTTP + WebSocket + 语音流水线
│   ├── storage/    SQLite 会话
│   └── main.cpp
├── web/            前端静态页面
├── tests/          单元 / 集成测试
├── models/         语音模型（不入库）
├── third_party/    预编译库与 LLM（不入库）
├── echo.json       运行配置
├── build_echo.bat  Windows 一键编译
└── DESIGN.md       设计文档
```

## 测试

```bash
# macOS / Linux
./build/bin/echo_tests

# Windows
.\build\bin\Release\echo_tests.exe
```

缺少模型时相关用例会自动跳过。

## 跨平台说明

- 平台相关代码统一走 `#if defined(_WIN32) / __APPLE__ / __linux__`
  条件编译；Windows 保留 `ReadConsoleW`、`ShellExecuteW`、
  `MEMORYSTATUSEX` 等原生 API。
- `src/engine/llm/genai_llm.h` 的 `Chat` 采用回调形式而非 `std::generator`，
  因为 Apple libc++ (macOS 26 SDK 附带的 libc++ 21) 尚未附带
  C++23 `<generator>` header。
- 官方 `ort_genai.h` 里若干内联函数返回 `std::unique_ptr<OgaTensor>`
  但 `OgaTensor` 定义在其后，MSVC 宽松、libc++/libstdc++ 严格。
  CMake configure 阶段会对该 header 做**幂等补丁**（把 `OgaTensor`
  提到 `OgaGenerator` 之前，并移除未使用的 `EncodeBatch`）——
  详见 [src/CMakeLists.txt](src/CMakeLists.txt) 里 `ort_genai.h 幂等补丁` 一节。
