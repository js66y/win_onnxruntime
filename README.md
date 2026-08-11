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
| 语言 / 构建 | C++23 · CMake · MSVC |
| 网络 | Boost.Asio + Beast（HTTP + WebSocket） |
| LLM | onnxruntime-genai · Qwen3-0.6B INT4（CPU） |
| ASR | sherpa-onnx · SenseVoice Small int8 |
| TTS | sherpa-onnx · MeloTTS 中英（VITS） |
| VAD | Silero VAD |
| 存储 | SQLite |

更完整的设计见 [DESIGN.md](DESIGN.md)。

## 环境要求

- Windows 10/11 x64
- Visual Studio 2022/2026（含 C++ 桌面开发与 CMake）
- Git + [Git LFS](https://git-lfs.com/)（下载模型需要）
- 建议内存 ≥ 8 GB；首次加载模型会占用数 GB 磁盘

## 快速开始

### 1. 克隆仓库

```powershell
git clone https://github.com/JS66y/echo-assistant.git
cd echo-assistant
```

### 2. 准备第三方库与模型

`third_party/` 与 `models/` **不入库**，需自行准备（体积较大）。

**语音模型（放入 `models/`）：**

```powershell
mkdir models
cd models

# ASR: SenseVoice
git clone --depth 1 https://huggingface.co/csukuangfj/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17

# TTS: MeloTTS 中英
git clone --depth 1 https://huggingface.co/csukuangfj/vits-melo-tts-zh_en

# VAD: Silero
# 从 sherpa-onnx 发布包或官方渠道获取 silero_vad.onnx，放到 models/silero_vad.onnx
cd ..
```

**LLM 与运行时（放入 `third_party/`）：**

- `third_party/onnxruntime-genai/`：预编译 include / lib / dll
- `third_party/sherpa-onnx/`：预编译 include / lib / dll
- `third_party/Qwen3-0.6B-ONNX-INT4-CPU/`：ONNX 量化对话模型
- `third_party/boost-1.87.0-cmake.tar.xz`：构建时由 CMake 解压（也可按 `CMakeLists.txt` 说明自行下载）
- `third_party/sqlite/`：SQLite 静态库（若工程已按此路径链接）

具体版本与目录布局需与本机 `CMakeLists.txt` / `src/CMakeLists.txt` 中的查找路径一致。

### 3. 编译

按本机 Visual Studio / CMake 安装路径，必要时编辑 `build_echo.bat` 中的 `VCVARS` 与 `CMAKE`，然后：

```powershell
.\build_echo.bat
# 全量重建: .\build_echo.bat clean
```

成功后得到：`build\bin\Release\echo.exe`

### 4. 运行

在仓库根目录运行（以便找到 `echo.json`、`web/`、`models/`）：

```powershell
# 网页语音助手（推荐）
.\build\bin\Release\echo.exe --serve

# 命令行纯文字聊天
.\build\bin\Release\echo.exe

# 语音文件：wav 进 → 识别 → 回答 → wav 出
.\build\bin\Release\echo.exe --voice question.wav answer.wav
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

```powershell
.\build\bin\Release\echo_tests.exe
```

缺少模型时相关用例会自动跳过。

