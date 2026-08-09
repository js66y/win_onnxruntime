"use strict";

// ============ 元素引用 ============
const chatEl = document.getElementById("chat");
const inputEl = document.getElementById("input");
const sendBtn = document.getElementById("btn-send");
const stopBtn = document.getElementById("btn-stop");
const resetBtn = document.getElementById("btn-reset");
const connDot = document.getElementById("conn-dot");
const modelNameEl = document.getElementById("model-name");
const welcomeEl = document.getElementById("welcome");

// ============ 状态 ============
let ws = null;
let generating = false;   // 正在等待/接收 LLM 输出
let currentBubble = null; // 正在流式追加的助手气泡
let reconnectDelay = 500;

// ============ WebSocket ============
function connect() {
  ws = new WebSocket(`ws://${location.host}/ws`);

  ws.onopen = () => {
    connDot.className = "dot dot-on";
    reconnectDelay = 500;
  };

  ws.onclose = () => {
    connDot.className = "dot dot-off";
    modelNameEl.textContent = "连接断开,重连中…";
    setGenerating(false);
    // 指数退避重连
    setTimeout(connect, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 2, 8000);
  };

  ws.onmessage = (event) => {
    const msg = JSON.parse(event.data);
    switch (msg.type) {
      case "hello":
        modelNameEl.textContent = `${msg.model} · 本地推理`;
        break;
      case "llm_delta":
        appendDelta(msg.text);
        break;
      case "llm_end":
        finishGeneration(msg);
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

// ============ 消息渲染 ============
function hideWelcome() {
  if (welcomeEl) welcomeEl.classList.add("hidden");
}

function scrollToBottom() {
  chatEl.scrollTop = chatEl.scrollHeight;
}

function addMessage(role, text) {
  hideWelcome();
  const msg = document.createElement("div");
  msg.className = `msg msg-${role}`;
  const bubble = document.createElement("div");
  bubble.className = "bubble";
  bubble.textContent = text;
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
  setGenerating(false);
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
  inputEl.value = "";
  autoResize();
}

sendBtn.addEventListener("click", () => sendChat(inputEl.value));

stopBtn.addEventListener("click", () => {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: "stop" }));
  }
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
