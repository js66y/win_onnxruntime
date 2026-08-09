# Echo(回声)全离线 AI 语音助手 — 设计文档 v1.0

> 结合 onnxruntime-genai(本地大模型)与 sherpa-onnx(语音识别/合成),
> 用 C++ 实现推理服务端,浏览器作为前端界面的实时语音对话助手。
> 项目定位:个人 C++ 进阶实战项目,目标是完成一个可发布、可演示的完整产品。

---

## 1. 项目概述

用户打开网页,按住说话(或开启免手动模式),语音经 WebSocket 实时传给 C++ 服务端;
服务端完成 VAD → 语音识别 → 大模型生成 → 语音合成,把回答的语音和文字流式推回浏览器播放。
全过程离线运行,不依赖任何云端 AI 服务。

核心卖点:

- **全离线、隐私安全**:所有模型本地运行,聊天内容不出机器;
- **真流式、低延迟**:大模型边生成边合成边播放,不等整段回答完成;
- **可打断(barge-in)**:助手说话时用户随时插话,助手立即停下来听;
- **实时字幕**:识别的中间结果实时上屏,像真人速记;
- **同一套代码两种形态**:本机运行(localhost 网页)或部署到服务器供公网访问。

## 2. 功能清单

### MVP(必须做)

| 功能 | 说明 |
|---|---|
| 文字聊天 | 网页输入文字,LLM 流式回答(打字机效果) |
| 语音输入 | 浏览器采集麦克风,服务端 VAD + ASR 转文字 |
| 语音输出 | 回答按句切分逐句 TTS,流式推回浏览器播放 |
| 实时字幕 | ASR 中间结果与最终结果实时显示 |
| 会话管理 | 多轮上下文、新建会话、历史记录持久化(SQLite) |

### 进阶(第二梯队)

| 功能 | 说明 |
|---|---|
| 语音打断 | 播放中检测到用户说话,立刻停止合成与播放 |
| 免手动全双工模式 | 不按按钮,VAD 自动判断说话起止 |
| 角色/人设切换 | 多套系统提示词 + TTS 音色/语速可选 |
| 本地工具调用 | LLM 可调用:查时间、计算器、系统信息等本地工具 |
| 多路并发会话 | 服务端同时服务多个浏览器连接(公网部署需要) |

### 可选(锦上添花)

- 说话人识别(sherpa-onnx speaker-id):区分家庭成员,记住各自偏好;
- 关键词唤醒(KWS):说"你好回声"激活;
- 语音消息导出、对话导出 Markdown。

## 3. 总体架构

```
┌─────────────── 浏览器(前端) ───────────────┐
│  麦克风采集(AudioWorklet, 16kHz PCM)         │
│  音频播放(Web Audio, 流式队列)               │
│  聊天界面 / 实时字幕 / 会话列表               │
└──────┬───────────────────────▲──────────────┘
       │ WebSocket(二进制音频 + JSON 控制消息)  │
┌──────▼───────────────────────┴──────────────┐
│           C++ 服务端(本项目主体)             │
│                                              │
│  网关层  HTTP 静态文件 + WebSocket 会话管理    │
│  管线层  每会话一条流水线:                    │
│    音频入队 → VAD → ASR → 对话管理             │
│    → LLM 流式生成 → 句子切分 → TTS → 音频出队  │
│  引擎层  SherpaAsr / SherpaTts / SileroVad     │
│          GenAiLlm(onnxruntime-genai, DML)     │
│  基础层  日志 / 配置 / 错误处理 / 存储(SQLite) │
└──────────────────────────────────────────────┘
```

### 数据流(一次语音对话)

1. 浏览器持续推送 20ms 一帧的 16kHz PCM;
2. 服务端 VAD 判定"开始说话/结束说话",切出语音段;
3. 语音段送 ASR 得到文本(中间结果实时回推做字幕);
4. 文本进对话管理器,拼装历史 + 系统提示词,喂给 LLM;
5. LLM 流式吐 token,按标点切句,每凑齐一句立即送 TTS;
6. TTS 合成的 PCM 分块推回浏览器,边收边播;
7. 播放期间若 VAD 检测到用户说话 → 广播打断信号,LLM 停止生成、TTS 队列清空、浏览器停播。

## 4. 技术选型

| 部分 | 选型 | 理由 |
|---|---|---|
| 语言标准 | C++20(局部尝试 C++23) | 覆盖协程、concepts、ranges;`std::expected` 用 C++23(MSVC 已支持) |
| 构建 | CMake + vcpkg + MSVC | Windows 生态最顺;vcpkg 一键装 Boost/SQLite 等 |
| 网络 | Boost.Asio + Boost.Beast | HTTP/WebSocket 一体;Asio 可写 C++20 协程,学习价值高 |
| LLM 推理 | onnxruntime-genai,DirectML EP | 你的 AMD/Intel 显卡走 DML 加速,CPU 兜底 |
| LLM 模型 | Qwen2.5-1.5B/3B-Instruct int4(中文强);备选 Phi-3.5-mini int4 | 中文对话质量与显存占用平衡 |
| ASR | sherpa-onnx + SenseVoice-small(VAD 切段,中英双语);进阶换流式 Zipformer | SenseVoice 准确率高、速度快;流式字幕后期升级 |
| VAD | silero-vad(onnx) | 事实标准,sherpa-onnx 原生支持 |
| TTS | sherpa-onnx + matcha-icefall-zh 或 kokoro 多语种 | 中文自然度较好,CPU 实时 |
| 存储 | SQLite(vcpkg: sqlite3) | 会话历史持久化 |
| 前端 | 原生 HTML/JS(无构建步骤)+ 现代化 CSS | 重心留给 C++;单页应用,静态文件由服务端直接托管 |
| 测试 | Catch2 或 GoogleTest | 引擎封装层做单元测试 |
| 日志 | spdlog | 事实标准 |

### WebSocket 协议(草案)

- 二进制帧:音频数据(上行 PCM16/16k,下行 PCM16/24k,带 4 字节类型头);
- 文本帧:JSON 控制消息,如
  `{"type":"asr_partial","text":"今天天气"}`
  `{"type":"llm_delta","text":"好的,"}`
  `{"type":"interrupt"}` `{"type":"tts_end"}` 等;
- 每连接一个会话上下文,心跳保活,断线重连恢复会话。

## 5. 模块设计

```
src/
├── common/     错误处理 Result<T>(std::expected)、配置加载、日志、UDL 时长字面量
├── engine/     推理引擎封装(全部 RAII + pimpl,禁止拷贝、支持移动)
│   ├── vad     SileroVad:feed(帧) → 事件{说话开始/结束, 语音段}
│   ├── asr     SherpaAsr:recognize(语音段) → 文本
│   ├── tts     SherpaTts:synthesize(句子) → PCM 块序列(生成器)
│   └── llm     GenAiLlm:chat(历史) → 协程 generator<token>,支持取消
├── pipeline/   会话流水线:线程编排、无锁音频环形缓冲、
│               有界阻塞队列(条件变量)、打断控制(atomic)
├── dialog/     对话历史、提示词模板、句子切分(regex)、工具调用(variant 分发)
├── server/     Asio/Beast:HTTP 静态托管 + WebSocket 会话、JSON 协议编解码
├── storage/    SQLite 封装:会话与消息的持久化
└── main.cpp    组装与启动
web/            前端静态文件(index.html / app.js / worklet.js / style.css)
tests/          单元测试
models/         模型文件(不入库,提供下载脚本)
```

### 关键类草图

- `template<class T> class BoundedQueue` — 有界阻塞队列(mutex + condition_variable),管线各级之间的通道;
- `class AudioRingBuffer` — 单生产者单消费者无锁环形缓冲(atomic + 内存序);
- `class Pipeline` — 持有各引擎与线程,状态机:空闲→聆听→思考→说话,`std::stop_token` 贯穿实现打断;
- `generator<std::string> GenAiLlm::chat(...)` — C++23 `std::generator`(或自写协程 generator)流式产出 token;
- `concept SpeechEngine` — 约束引擎接口,单测中用 mock 引擎替换真实模型。

## 6. C++ 大纲知识点映射(巩固重点)

| 大纲段落 | 在项目中的落点 |
|---|---|
| 13~19 指针/类/继承/多态/运算符重载 | 引擎基类与多态接口、Result 类型的运算符设计 |
| 20~26 模板/命名空间/异常/类型转换/函数指针 | BoundedQueue 类模板、边界层异常→expected 的转换 |
| 27~29 STL/map/函数绑定 | 工具调用注册表 `map<string, function>`、协议路由 |
| 30~35 智能指针/lambda/移动/完美转发 | 引擎所有权(unique_ptr)、回调、PCM 块零拷贝移动 |
| 36~40 constexpr/decltype/特殊成员/traits/算法 | 编译期协议常量、五法则实践、音频处理用 algorithm |
| 41~50 线程/锁/条件变量/信号量/async/屏障 | **流水线主战场**:多线程编排、并发会话限流(semaphore) |
| 51~52 协程 | LLM 流式生成 generator、Asio 协程处理网络 IO |
| 53~55 原子/无锁/内存序 | 打断标志、无锁环形缓冲 |
| 56~61 concepts/modules/ranges/any/variant/expected | 引擎接口约束、文本处理、协议消息 variant、错误处理 |
| 62~68 CPO/regex/UDL/CRTP/变参模板/飞船 | 句子切分 regex、`500ms` 配置字面量、日志变参模板 |

## 7. 里程碑计划

| 阶段 | 目标(每阶段结束都有可运行成果) | 预估 |
|---|---|---|
| M0 | 环境搭建;分别跑通 genai 与 sherpa-onnx 官方 C++ demo | 1 周 |
| M1 | 命令行流式聊天机器人(仅 LLM);common 基础层成型 | 1~2 周 |
| M2 | ASR/TTS/VAD 引擎封装 + 单元测试;命令行"wav 进 wav 出" | 1~2 周 |
| M3 | Beast 服务端 + 网页文字聊天(SSE 式流式回答) | 2 周 |
| M4 | 实时语音:麦克风上行、VAD+ASR、逐句 TTS 下行、实时字幕 | 2~3 周 |
| M5 | 打断、全双工模式、会话持久化、角色切换、工具调用 | 2~3 周 |
| M6 | 打磨 UI、并发压测、打包发布、(可选)公网部署 | 1~2 周 |

## 8. 上线方案(两种形态,同一套代码)

- **形态 A:本机运行(首选起步)** — 用户下载发布包(exe + 模型下载脚本),
  双击运行后浏览器打开 `http://localhost:8080` 使用。GitHub 开源 + Release 分发。
- **形态 B:公网网站(可选)** — 同一服务端部署到云服务器供任何人访问。
  注意:云服务器无 DirectML(Linux/无显卡),LLM 走 CPU,需选 1.5B int4 级别模型,
  并做并发排队/限流;4 核 8G 起步可跑演示。适合做在线 Demo,重度使用仍推荐形态 A。

## 9. 风险与对策

| 风险 | 对策 |
|---|---|
| DirectML 下大模型速度不理想 | 降级 1.5B 模型或 CPU int4;流式输出掩盖首字延迟 |
| 中文 TTS 自然度一般 | 备选 3 套模型(matcha / kokoro / melo),抽象引擎接口随时切换 |
| SenseVoice 非流式,字幕延迟到句尾 | M4 先接受;后续升级流式 Zipformer 出中间结果 |
| Beast/Asio 上手陡峭 | M3 只做最小 HTTP+WS;参考 Beast 官方 websocket 示例起步 |
| 浏览器音频兼容性 | 统一 AudioWorklet + 16k 重采样;只承诺 Chrome/Edge |

## 10. 已确认的决策记录

- 方向:通用离线语音助手(不含 C++ 教学功能);
- 硬件:AMD/Intel 显卡 → onnxruntime-genai 用 DirectML,sherpa-onnx 用 CPU;
- 界面:网页(浏览器前端 + C++ 服务端);
- 上线:确定采用形态 A —— 本机运行 + localhost 网页界面,GitHub 开源并发布 Release;公网 Demo 以后视情况再做;
- 项目名:Echo(回声),仓库名建议 `echo-assistant`,C++ 顶层命名空间 `echo`;
- 学习背景:大纲已过一遍,项目用于巩固,可直接采用现代 C++ 风格。
