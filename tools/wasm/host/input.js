// Console input queue shared between the machine (writer, a thread that
// never blocks) and the kernel threads (readers, from the console_read
// import). A single-producer ring buffer in a SharedArrayBuffer, because a
// kernel thread cannot receive messages while it is blocked or running.

const HEAD = 0; // next byte to read (index into data, modulo capacity)
const TAIL = 1; // next byte to write
const HEADER_BYTES = 8;

export class InputQueue {
  // Pass nothing to create a queue, or the `buffer` of an existing one
  // (it survives postMessage) to attach to it.
  constructor(buffer = new SharedArrayBuffer(HEADER_BYTES + 4096)) {
    this.buffer = buffer;
    this.index = new Int32Array(buffer, 0, 2);
    this.data = new Uint8Array(buffer, HEADER_BYTES);
  }

  // Returns how many bytes of `bytes` were queued (fewer if full).
  write(bytes) {
    const capacity = this.data.length;
    const head = Atomics.load(this.index, HEAD);
    let tail = Atomics.load(this.index, TAIL);
    let written = 0;
    while (written < bytes.length && tail - head < capacity) {
      this.data[tail % capacity] = bytes[written++];
      tail++;
    }
    Atomics.store(this.index, TAIL, tail);
    return written;
  }

  // Copies up to target.length bytes into `target`; returns the count.
  read(target) {
    const capacity = this.data.length;
    const tail = Atomics.load(this.index, TAIL);
    let head = Atomics.load(this.index, HEAD);
    let count = 0;
    while (count < target.length && head < tail) {
      target[count++] = this.data[head % capacity];
      head++;
    }
    Atomics.store(this.index, HEAD, head);
    return count;
  }
}
