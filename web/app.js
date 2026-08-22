"use strict";

const chatEl = document.getElementById("chat");
const inputEl = document.getElementById("input");
const sendBtn = document.getElementById("btn-send");
const stopBtn = document.getElementById("btn-stop");
const resetBtn = document.getElementById("btn-reset");
const micBtn = document.getElementById("btn-mic");
const roleSelect = document.getElementById("role-select");
const connDot = document.getElementById("conn-dot");
const modelNameEl = document.getElementById("model-name");
const welcomeEl = document.getElementById("welcome");
const statusEl = document.getElementById("voice-status");

let ws = null;
let generating = false;
let voiceTurn = false;
let currentBubble = null;
let reconnectDelay = 500;
let ignoreRoleChange = false;

let voiceAvailable = false;
let ttsSampleRate = 44100;

let micOn = false;
let micCtx = null;
let micStream = null;
let micNode = null;

let playCtx = null;
let playDest = null;      // TTS 通过 MediaStreamDestination -> WebRTC 回环 -> <audio>
let playAudioEl = null;   // 承接回环下行的音频元素; 也是最终的扬声器输出
let nextPlayTime = 0;
let activeSources = [];

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
        fillRoles(msg.roles || [], msg.role);
        break;
      case "llm_delta":
        appendDelta(msg.text);
        break;
      case "llm_end":
        finishGeneration(msg);
        break;
      case "tool_result":
        addNotice(`🔧 ${msg.tool}`);
        break;
      case "mic_ready":
        setStatus("聆听中…(说话可打断)", "listening");
        break;
      case "speech_start":
        setStatus("正在说话…", "speech");
        break;
      case "asr_text":
        // 打断后开启新一轮: 清掉未完成的助手气泡
        if (currentBubble) {
          currentBubble.classList.remove("generating");
          currentBubble = null;
        }
        addMessage("user", msg.text, "🎤");
        setGenerating(true);
        voiceTurn = true;
        setStatus("思考中…", "thinking");
        break;
      case "interrupt":
        stopPlayback();
        if (currentBubble) {
          currentBubble.classList.remove("generating");
          const bubble = currentBubble.querySelector(".bubble");
          if (bubble && !bubble.textContent.trim()) currentBubble.remove();
          currentBubble = null;
        }
        voiceTurn = false;
        setGenerating(false);
        setStatus(micOn ? "聆听中…(说话可打断)" : "", micOn ? "listening" : "");
        addNotice("已打断");
        break;
      case "tts_end":
        voiceTurn = false;
        setGenerating(false);
        setStatus(micOn ? "聆听中…(说话可打断)" : "", micOn ? "listening" : "");
        break;
      case "reset_done":
        clearChat();
        addNotice("已开始新对话");
        break;
      case "role_changed":
        addNotice(`已切换角色: ${msg.name}`);
        break;
      case "error":
        addNotice(`出错: ${msg.message}`);
        setGenerating(false);
        break;
    }
  };
}

function fillRoles(roles, activeId) {
  ignoreRoleChange = true;
  roleSelect.innerHTML = "";
  for (const role of roles) {
    const opt = document.createElement("option");
    opt.value = role.id;
    opt.textContent = role.name;
    if (role.id === activeId) opt.selected = true;
    roleSelect.appendChild(opt);
  }
  ignoreRoleChange = false;
}

function handleBinaryFrame(arrayBuffer) {
  const view = new DataView(arrayBuffer);
  if (view.byteLength <= 4 || view.getUint8(0) !== 0x02) return;
  playPcm(new Int16Array(arrayBuffer, 4));
  setStatus("回答播报中…(说话可打断)", "speaking");
}

async function ensurePlayback() {
  if (playCtx) {
    if (playCtx.state === "suspended") await playCtx.resume();
    return;
  }
  playCtx = new AudioContext();

  // WebRTC AEC 回环: 浏览器的回声消除只对 WebRTC 音频轨生效, 直接送到
  // AudioContext.destination 的 TTS 对 AEC 是"看不见"的, 会被麦克风采到
  // 形成回环。这里让 TTS 走 MediaStreamDestination -> RTCPeerConnection
  // 环回一圈, 再作为远端流播放, AEC 就能拿到它作为参考信号扣除。
  playDest = playCtx.createMediaStreamDestination();
  const pcSend = new RTCPeerConnection();
  const pcRecv = new RTCPeerConnection();
  pcSend.onicecandidate = (e) => e.candidate && pcRecv.addIceCandidate(e.candidate);
  pcRecv.onicecandidate = (e) => e.candidate && pcSend.addIceCandidate(e.candidate);
  pcRecv.ontrack = (event) => {
    playAudioEl = new Audio();
    playAudioEl.autoplay = true;
    playAudioEl.srcObject = event.streams[0];
    playAudioEl.play().catch(() => { /* 用户手势前浏览器会拒绝, 忽略 */ });
  };
  for (const track of playDest.stream.getAudioTracks()) {
    pcSend.addTrack(track, playDest.stream);
  }
  const offer = await pcSend.createOffer();
  await pcSend.setLocalDescription(offer);
  await pcRecv.setRemoteDescription(offer);
  const answer = await pcRecv.createAnswer();
  await pcRecv.setLocalDescription(answer);
  await pcSend.setRemoteDescription(answer);
}

function playPcm(int16) {
  if (!playCtx || !playDest) return;  // 首帧应在 startMic 里预热完成
  if (playCtx.state === "suspended") playCtx.resume();

  const float32 = new Float32Array(int16.length);
  for (let i = 0; i < int16.length; i++) float32[i] = int16[i] / 32768;

  const buffer = playCtx.createBuffer(1, float32.length, ttsSampleRate);
  buffer.copyToChannel(float32, 0);

  const source = playCtx.createBufferSource();
  source.buffer = buffer;
  source.connect(playDest);

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
    try { source.stop(); } catch { /* already ended */ }
  }
  activeSources = [];
  nextPlayTime = 0;
}

async function startMic() {
  if (!voiceAvailable || micOn) return;
  try {
    micStream = await navigator.mediaDevices.getUserMedia({
      audio: {
        echoCancellation: true,
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

  // 借这次用户手势把 TTS 播放链路和 AEC 回环也建起来
  await ensurePlayback();

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

function setStatus(text, kind) {
  statusEl.textContent = text;
  statusEl.dataset.kind = kind || "";
}

function hideWelcome() {
  if (welcomeEl) welcomeEl.classList.add("hidden");
}

function clearChat() {
  chatEl.querySelectorAll(".msg, .notice").forEach((el) => el.remove());
  if (welcomeEl) {
    welcomeEl.classList.remove("hidden");
    chatEl.prepend(welcomeEl);
  }
  currentBubble = null;
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
    if (stats.new_tokens > 0) {
      const meta = document.createElement("div");
      meta.className = "meta";
      meta.textContent =
        `${stats.new_tokens} tokens · 首字 ${stats.first_token_seconds.toFixed(2)}s` +
        ` · ${stats.tokens_per_second.toFixed(1)} tok/s`;
      currentBubble.appendChild(meta);
    }
  }
  currentBubble = null;
  if (!voiceTurn) setGenerating(false);
}

function setGenerating(value) {
  generating = value;
  sendBtn.classList.toggle("hidden", value);
  stopBtn.classList.toggle("hidden", !value);
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
  setStatus(micOn ? "聆听中…(说话可打断)" : "", micOn ? "listening" : "");
});

resetBtn.addEventListener("click", () => {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: "new_session" }));
  }
  stopPlayback();
  setGenerating(false);
});

roleSelect.addEventListener("change", () => {
  if (ignoreRoleChange || !ws || ws.readyState !== WebSocket.OPEN) return;
  ws.send(JSON.stringify({ type: "set_role", id: roleSelect.value }));
});

inputEl.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && !event.shiftKey) {
    event.preventDefault();
    sendChat(inputEl.value);
  }
});

function autoResize() {
  inputEl.style.height = "auto";
  inputEl.style.height = `${Math.min(inputEl.scrollHeight, 140)}px`;
}
inputEl.addEventListener("input", autoResize);

document.querySelectorAll(".chip").forEach((chip) => {
  chip.addEventListener("click", () => sendChat(chip.dataset.text));
});

connect();
inputEl.focus();
