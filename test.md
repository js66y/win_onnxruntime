
```Markdown
随便记一记


llm(api和onnxruntime-genai两种)
vad-tts-asr(相对固定的使用sherpa-onnx)
agent(上下文)
session
pipeline
xlog(辅助开发用的log)
test_xcpp(用于写测试)
audio(向下接受不同的前端，web/qt/cmd/)


src/
  agent/      # facade + 组装（唯一对外头文件）
  pipeline/   # 状态机 / 打断 / 句子切分
  session/    # 历史 + 上下文 + stream 池
  speech/     # api/ + sherpa/  (vad asr tts 三 adapter)
  llm/        # llm_api/ + llm_genai/
  audio/      # ring buffer / resample / aec / frame
  frontend/   # cmd/ qt/ web/ embedded
  xlog/
tests/        # unit + sim + e2e
```
