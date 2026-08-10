"use strict";

// ============ 元素引用 ============
const chatEl = document.getElementById("chat");
const inputEl = document.getElementById("input");
const sendBtn = document.getElementById("btn-send");
const stopBtn = document.getElementById("btn-stop");
const resetBtn = document.getElementById("btn-reset");
const micBtn = document.getElementById("btn-mic");
const connDot = document.getElementById("conn-dot");
const modelNameEl = document.getElementById("model-name");
const welcomeEl = document.getElementById("welcome");
const statusEl = document.getElementById("voice-status");

// ============ 状态 ============
let ws = null;
let generating = false;    // 正在等待/接收 LLM 输出
let voiceTurn = false;     // 当前轮由语音发起(结束以 tts_end 为准)
let currentBubble = null;  // 正在流式追加的助手气泡
let reconnectDelay = 500;

let voiceAvailable = false;
let ttsSampleRate = 44100;

// 麦克风
let micOn = false;
let micCtx = null;
let micStream = null;
let micNode = null;

// 播放队列
let playCtx = null;
let nextPlayTime = 0;
let activeSources = [];

// ============ WebSocket ============
function connect() {
  ws = new WebSocket(`ws://${location.host}/ws`);
  ws.binaryType = "arraybuffer";

  ws.onopen = () => {
    connDot.className = "dot dot-on";
    reconnectDelay = 500;
  };

  ws.onclose = () => {
    connDot.className = "dot dot-off";
    modelNameEl.textContent = "连接断开,重连中…";
    setGenerating(false);
    stopMic();
    setTimeout(connect, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 2, 8000);
  };

  ws.onmessage = (event) => {
    if (event.data instanceof ArrayBuffer) {
      handleBinaryFrame(event.data);
      return;
    }
    const msg = JSON.parse(event.data);
    switch (msg.type) {
      case "hello":
        modelNameEl.textContent = `${msg.model} · 本地推理`;
        voiceAvailable = msg.voice;
        ttsSampleRate = msg.tts_sample_rate || 44100;
        micBtn.classList.toggle("hidden", !voiceAvailable);
        break;
      case "llm_delta":
        appendDelta(msg.text);
        break;
      case "llm_end":
        finishGeneration(msg);
        break;
      case "mic_ready":
        setStatus("聆听中…", "listening");
        break;
      case "speech_start":
        setStatus("正在说话…", "speech");
        break;
      case "asr_text":
        addMessage("user", msg.text, "🎤");
        setGenerating(true);
        voiceTurn = true;
        setStatus("思考中…", "thinking");
        break;
      case "tts_end":
        voiceTurn = false;
        setGenerating(false);
        setStatus(micOn ? "聆听中…" : "", micOn ? "listening" : "");
        break;
      case "reset_done":
        addNotice("对话已清空");
        break;
      case "error":
        addNotice(`出错: ${msg.message}`);
        setGenerating(false);
        break;
    }
  };
}

// ============ TTS 播放 ============
function handleBinaryFrame(arrayBuffer) {
  const view = new DataView(arrayBuffer);
  if (view.byteLength <= 4 || view.getUint8(0) !== 0x02) return;

  const pcm = new Int16Array(arrayBuffer, 4);
  playPcm(pcm);
  setStatus("回答播报中…", "speaking");
}

function playPcm(int16) {
  if (!playCtx) playCtx = new AudioContext();
  if (playCtx.state === "suspended") playCtx.resume();

  const float32 = new Float32Array(int16.length);
  for (let i = 0; i < int16.length; i++) float32[i] = int16[i] / 32768;

  const buffer = playCtx.createBuffer(1, float32.length, ttsSampleRate);
  buffer.copyToChannel(float32, 0);

  const source = playCtx.createBufferSource();
  source.buffer = buffer;
  source.connect(playCtx.destination);

  const startAt = Math.max(playCtx.currentTime + 0.05, nextPlayTime);
  source.start(startAt);
  nextPlayTime = startAt + buffer.duration;

  activeSources.push(source);
  source.onended = () => {
    activeSources = activeSources.filter((s) => s !== source);
  };
}

function stopPlayback() {
  for (const source of activeSources) {
    try { source.stop(); } catch { /* 已结束 */ }
  }
  activeSources = [];
  nextPlayTime = 0;
}

// ============ 麦克风 ============
async function startMic() {
  if (!voiceAvailable || micOn) return;
  try {
    micStream = await navigator.mediaDevices.getUserMedia({
      audio: {
        echoCancellation: true,   // 回声消除: 避免拾到 TTS 播报
        noiseSuppression: true,
        autoGainControl: true,
        channelCount: 1,
      },
    });
  } catch (err) {
    addNotice(`无法访问麦克风: ${err.message}`);
    return;
  }

  micCtx = new AudioContext({ sampleRate: 16000 });
  await micCtx.audioWorklet.addModule("worklet.js");
  micNode = new AudioWorkletNode(micCtx, "pcm-capture");
  micNode.port.onmessage = (event) => sendAudioFrame(event.data);
  micCtx.createMediaStreamSource(micStream).connect(micNode);

  micOn = true;
  micBtn.classList.add("mic-on");
  ws.send(JSON.stringify({ type: "mic_start" }));
}

function stopMic() {
  if (!micOn) return;
  micOn = false;
  micBtn.classList.remove("mic-on");
  setStatus("", "");

  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: "mic_stop" }));
  }
  if (micNode) { micNode.disconnect(); micNode = null; }
  if (micCtx) { micCtx.close(); micCtx = null; }
  if (micStream) {
    micStream.getTracks().forEach((track) => track.stop());
    micStream = null;
  }
}

function sendAudioFrame(int16) {
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  const frame = new ArrayBuffer(4 + int16.byteLength);
  new Uint8Array(frame)[0] = 0x01;
  new Int16Array(frame, 4).set(int16);
  ws.send(frame);
}

micBtn.addEventListener("click", () => (micOn ? stopMic() : startMic()));

// ============ 状态条 ============
function setStatus(text, kind) {
  statusEl.textContent = text;
  statusEl.dataset.kind = kind || "";
}

// ============ 消息渲染 ============
function hideWelcome() {
  if (welcomeEl) welcomeEl.classList.add("hidden");
}

function scrollToBottom() {
  chatEl.scrollTop = chatEl.scrollHeight;
}

function addMessage(role, text, badge) {
  hideWelcome();
  const msg = document.createElement("div");
  msg.className = `msg msg-${role}`;
  const bubble = document.createElement("div");
  bubble.className = "bubble";
  bubble.textContent = badge ? `${badge} ${text}` : text;
  msg.appendChild(bubble);
  chatEl.appendChild(msg);
  scrollToBottom();
  return msg;
}

function addNotice(text) {
  hideWelcome();
  const el = document.createElement("div");
  el.className = "notice";
  el.textContent = text;
  chatEl.appendChild(el);
  scrollToBottom();
}

function appendDelta(text) {
  if (!currentBubble) {
    const msg = addMessage("bot", "");
    msg.classList.add("generating");
    currentBubble = msg;
  }
  currentBubble.querySelector(".bubble").textContent += text;
  scrollToBottom();
}

function finishGeneration(stats) {
  if (currentBubble) {
    currentBubble.classList.remove("generating");
    const meta = document.createElement("div");
    meta.className = "meta";
    meta.textContent =
      `${stats.new_tokens} tokens · 首字 ${stats.first_token_seconds.toFixed(2)}s` +
      ` · ${stats.tokens_per_second.toFixed(1)} tok/s`;
    currentBubble.appendChild(meta);
  }
  currentBubble = null;
  // 语音轮次还有 TTS 在播, 等 tts_end 再解除生成态
  if (!voiceTurn) setGenerating(false);
}

// ============ 交互 ============
function setGenerating(value) {
  generating = value;
  sendBtn.classList.toggle("hidden", value);
  stopBtn.classList.toggle("hidden", !value);
  inputEl.disabled = false;
}

function sendChat(text) {
  const trimmed = text.trim();
  if (!trimmed || generating || !ws || ws.readyState !== WebSocket.OPEN) return;

  addMessage("user", trimmed);
  ws.send(JSON.stringify({ type: "chat", text: trimmed }));
  setGenerating(true);
  voiceTurn = false;
  inputEl.value = "";
  autoResize();
}

sendBtn.addEventListener("click", () => sendChat(inputEl.value));

stopBtn.addEventListener("click", () => {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: "stop" }));
  }
  stopPlayback();
  voiceTurn = false;
  setGenerating(false);
  setStatus(micOn ? "聆听中…" : "", micOn ? "listening" : "");
});

resetBtn.addEventListener("click", () => {
  if (generating) return;
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: "reset" }));
  }
});

inputEl.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && !event.shiftKey) {
    event.preventDefault();
    sendChat(inputEl.value);
  }
});

// 输入框高度自适应
function autoResize() {
  inputEl.style.height = "auto";
  inputEl.style.height = `${Math.min(inputEl.scrollHeight, 140)}px`;
}
inputEl.addEventListener("input", autoResize);

// 欢迎页快捷提问
document.querySelectorAll(".chip").forEach((chip) => {
  chip.addEventListener("click", () => sendChat(chip.dataset.text));
});

connect();
inputEl.focus();
