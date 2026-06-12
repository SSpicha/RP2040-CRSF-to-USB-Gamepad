import type { DeviceStatus, MapPayload } from "./types";

export type IncomingMessage = DeviceStatus | MapPayload | Record<string, unknown>;

export class SerialService {
  private port: SerialPort | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private readLoopRunning = false;
  private decoder = new TextDecoder();
  private encoder = new TextEncoder();
  private lastMessageTime = 0;
  private watchdogTimer: number | null = null;
  private readTimeoutMs = 5000;
  private staleThresholdMs = 10000;

  private async readWithTimeout(): Promise<any> {
    if (!this.reader) throw new Error("Serial reader is not available.");
    return Promise.race([
      this.reader.read(),
      new Promise<never>((_, reject) =>
        setTimeout(() => reject(new Error("read timeout")), this.readTimeoutMs)
      )
    ]);
  }

  private startWatchdog(onStale: () => void): void {
    this.stopWatchdog();
    this.lastMessageTime = Date.now();
    this.watchdogTimer = window.setInterval(() => {
      if (this.readLoopRunning && Date.now() - this.lastMessageTime > this.staleThresholdMs) {
        onStale();
      }
    }, 1000);
  }

  private stopWatchdog(): void {
    if (this.watchdogTimer !== null) {
      window.clearInterval(this.watchdogTimer);
      this.watchdogTimer = null;
    }
  }

  setReadTimeout(ms: number): void {
    this.readTimeoutMs = ms;
  }

  setStaleThreshold(ms: number): void {
    this.staleThresholdMs = ms;
  }

  getLastMessageAgeMs(): number {
    return Date.now() - this.lastMessageTime;
  }

  async connect(baudRate = 115200): Promise<void> {
    if (!("serial" in navigator)) {
      throw new Error("Web Serial API is not supported in this browser.");
    }
    this.port = await (navigator as Navigator).serial.requestPort();
    await this.port.open({ baudRate });
    this.reader = this.port.readable?.getReader() ?? null;
    this.writer = this.port.writable?.getWriter() ?? null;

    this.readLoopRunning = true;
  }

  async disconnect(): Promise<void> {
    this.readLoopRunning = false;
    this.stopWatchdog();
    if (this.reader) {
      try { await this.reader.cancel(); } catch {}
      this.reader.releaseLock();
      this.reader = null;
    }
    if (this.writer) {
      this.writer.releaseLock();
      this.writer = null;
    }
    if (this.port) {
      await this.port.close();
      this.port = null;
    }
  }

  async send(command: string): Promise<void> {
    if (!this.writer) throw new Error("Serial writer is not available.");
    await this.writer.write(this.encoder.encode(`${command}\n`));
  }

  async startReadLoop(onMessage: (msg: IncomingMessage) => void, onStale?: () => void): Promise<void> {
    if (!this.reader) throw new Error("Serial reader is not available.");
    let buffer = "";

    this.startWatchdog(onStale ?? (() => {}));

    while (this.readLoopRunning) {
      let value: Uint8Array | null = null;
      try {
        const result = await this.readWithTimeout();
        const { value: readValue, done } = result;
        if (done) break;
        value = readValue ?? null;
      } catch (err) {
        if (!this.readLoopRunning) break;
        console.warn("SerialService: read error", err);
        onMessage({ type: "error", message: (err as Error).message });
        continue;
      }

      if (!value) continue;
      buffer += this.decoder.decode(value);
      this.lastMessageTime = Date.now();

      let newlineIndex = buffer.indexOf("\n");
      while (newlineIndex >= 0) {
        const line = buffer.slice(0, newlineIndex).trim();
        buffer = buffer.slice(newlineIndex + 1);
        if (line.length === 0) {
          newlineIndex = buffer.indexOf("\n");
          continue;
        }

        const trimmed = line.trim();
        if (trimmed.startsWith("{") && trimmed.endsWith("}")) {
          try {
            const parsed = JSON.parse(trimmed) as Record<string, unknown>;
            const messageType = typeof parsed.type === "string" ? parsed.type : null;
            if (!messageType) {
              throw new Error("missing type");
            }
            const { type, ...rest } = parsed;
            const message: IncomingMessage =
              type === "status" || type === "map"
                ? ({ type, ...rest } as DeviceStatus | MapPayload)
                : { type, ...rest };
            onMessage(message);
          } catch (error) {
            console.warn("SerialService: failed to parse JSON line", trimmed, error);
            onMessage({ type: "parse_error", raw: trimmed });
          }
          newlineIndex = buffer.indexOf("\n");
          continue;
        }

        onMessage({ type: "line", raw: trimmed });
        newlineIndex = buffer.indexOf("\n");
      }
    }

    this.stopWatchdog();
  }

  isConnected(): boolean {
    return this.port !== null;
  }
}
