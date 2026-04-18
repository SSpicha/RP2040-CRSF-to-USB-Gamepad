import type { DeviceStatus, MapPayload } from "./types";

export type IncomingMessage = DeviceStatus | MapPayload | Record<string, unknown>;

export class SerialService {
  private port: SerialPort | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private readLoopRunning = false;
  private decoder = new TextDecoder();
  private encoder = new TextEncoder();

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
    if (this.reader) {
      await this.reader.cancel();
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

  async startReadLoop(onMessage: (msg: IncomingMessage) => void): Promise<void> {
    if (!this.reader) throw new Error("Serial reader is not available.");
    let buffer = "";

    while (this.readLoopRunning) {
      const { value, done } = await this.reader.read();
      if (done) break;
      if (!value) continue;
      buffer += this.decoder.decode(value);

      let newlineIndex = buffer.indexOf("\n");
      while (newlineIndex >= 0) {
        const line = buffer.slice(0, newlineIndex).trim();
        buffer = buffer.slice(newlineIndex + 1);
        if (line.startsWith("{") && line.endsWith("}")) {
          try {
            onMessage(JSON.parse(line) as IncomingMessage);
          } catch {
            onMessage({ type: "parse_error", raw: line });
          }
        } else if (line.length > 0) {
          onMessage({ type: "line", raw: line });
        }
        newlineIndex = buffer.indexOf("\n");
      }
    }
  }

  isConnected(): boolean {
    return this.port !== null;
  }
}
