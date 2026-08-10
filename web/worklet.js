// 麦克风采集处理器: 运行在音频线程。
// AudioContext 以 16kHz 创建, 这里收到的就是 16kHz float32,
// 攒够 20ms(320 样本)转 Int16 发给主线程。
class PcmCaptureProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.frameSize = 320;
    this.buffer = new Int16Array(this.frameSize);
    this.offset = 0;
  }

  process(inputs) {
    const channel = inputs[0] && inputs[0][0];
    if (!channel) return true;

    for (let i = 0; i < channel.length; i++) {
      const clamped = Math.max(-1, Math.min(1, channel[i]));
      this.buffer[this.offset++] = clamped * 32767;
      if (this.offset === this.frameSize) {
        // 拷贝一份转移所有权, 内部缓冲继续复用
        const frame = this.buffer.slice();
        this.port.postMessage(frame, [frame.buffer]);
        this.offset = 0;
      }
    }
    return true;
  }
}

registerProcessor("pcm-capture", PcmCaptureProcessor);
