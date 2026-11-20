// A virtio block device on top of a "disk":
//
//   { read(view, position) -> bytes read, write(view, position), flush() }
//
// all synchronous; `view` is a Uint8Array of kernel memory.

import { VIRTIO_F_VERSION_1, VIRTIO_ID_BLOCK } from "./virtio.js";

const SECTOR = 512;
const F_SEG_MAX = 2;
const F_RO = 5;
const F_FLUSH = 9;
const SEG_MAX = 126; // plus header and status: half of a 256 entry queue

const T_IN = 0;
const T_OUT = 1;
const T_FLUSH = 4;
const T_GET_ID = 8;
const S_OK = 0;
const S_IOERR = 1;
const S_UNSUPP = 2;

// The description of a disk of `bytes` bytes (see virtio.js); the caller
// adds `control`.
export function blkDevice(bytes, readOnly = false) {
  const config = new Uint8Array(60); // struct virtio_blk_config
  const view = new DataView(config.buffer);
  view.setBigUint64(0, BigInt(Math.floor(bytes / SECTOR)), true); // capacity
  view.setUint32(12, SEG_MAX, true); // seg_max
  return {
    id: VIRTIO_ID_BLOCK,
    featuresLo: (1 << F_SEG_MAX) | (1 << F_FLUSH) | (readOnly ? 1 << F_RO : 0),
    featuresHi: 1 << (VIRTIO_F_VERSION_1 - 32),
    config,
  };
}

// Returns handle(chain) for VirtQueue.process.
export function blkHandler(memory, disk) {
  const bytes = (addr, len) => new Uint8Array(memory.buffer, addr, len);

  return (chain) => {
    const header = chain[0];
    const status = chain[chain.length - 1];
    const data = chain.slice(1, -1);
    if (chain.length < 2 || header.len < 16 || !status.write || status.len < 1)
      throw new Error("virtio-blk: malformed request");

    const view = new DataView(memory.buffer, header.addr, 16);
    const type = view.getUint32(0, true);
    let position =
      (view.getUint32(8, true) + view.getUint32(12, true) * 2 ** 32) * SECTOR;
    let written = 0;
    let result = S_OK;
    try {
      switch (type) {
        case T_IN:
          for (const { addr, len } of data) {
            const target = bytes(addr, len);
            const n = disk.read(target, position);
            if (n < len) target.fill(0, n);
            position += len;
            written += len;
          }
          break;
        case T_OUT:
          for (const { addr, len } of data) {
            disk.write(bytes(addr, len), position);
            position += len;
          }
          break;
        case T_FLUSH:
          disk.flush();
          break;
        case T_GET_ID: {
          const id = new TextEncoder().encode("linux-wasm-disk");
          const target = bytes(data[0].addr, data[0].len);
          target.fill(0);
          target.set(id.subarray(0, target.length));
          written += target.length;
          break;
        }
        default:
          result = S_UNSUPP;
      }
    } catch (e) {
      console.error(`virtio-blk: request type ${type} failed: ${e}`);
      result = S_IOERR;
    }
    bytes(status.addr, 1)[0] = result;
    return written + 1;
  };
}
