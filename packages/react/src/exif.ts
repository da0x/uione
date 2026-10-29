// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a camera wrote in a photo, its EXIF, found without decoding the photo: in a
// JPEG's APP1 segment, a PNG's eXIf chunk, a WebP's EXIF chunk, or a TIFF, which is
// what EXIF itself is. Only what's asked for is read, every offset and count checked
// against what's there, and every walk bounded, so a file cut short or made wrong
// reads as far as it's sound and no further. The same as the desktop's.

// When it was taken, as it says it, like 2026:10:12 08:30:15, and its offset from UTC,
// like +02:00, when it says one; its make and model; and which way up it was held, 1
// to 8 as EXIF counts them, 1 as it's stored.
export interface PhotoFacts {
  taken: string;
  offset: string;
  make: string;
  model: string;
  orientation: number;
}

// Bytes at an offset: up to `n`, fewer past the end.
type BytesAt = (at: number, n: number) => Promise<Uint8Array>;

const nothing = (): PhotoFacts => ({ taken: "", offset: "", make: "", model: "", orientation: 1 });

// A TIFF's structure, from `base` and no further than `end`: its first directory, for
// the camera and which way up, and the EXIF directory it points to, for when.
async function tiff(read: BytesAt, base: number, end: number): Promise<PhotoFacts> {
  const out = nothing();
  if (end < base + 8) return out;
  const head = await read(base, 8);
  if (head.length < 8) return out;
  const little = head[0] === 0x49 && head[1] === 0x49;
  if (!little && !(head[0] === 0x4d && head[1] === 0x4d)) return out;
  const u16 = (b: Uint8Array, at: number) => (little ? b[at]! | (b[at + 1]! << 8) : (b[at]! << 8) | b[at + 1]!);
  const u32 = (b: Uint8Array, at: number) => (little ? (b[at]! | (b[at + 1]! << 8) | (b[at + 2]! << 16)) + b[at + 3]! * 0x1000000 : b[at]! * 0x1000000 + ((b[at + 1]! << 16) | (b[at + 2]! << 8) | b[at + 3]!));
  if (u16(head, 2) !== 42) return out;
  const room = end - base;
  // An ASCII value, in the entry when it fits, or where it points; up to its first NUL,
  // only what's printable, without the spaces a camera pads it with.
  const text = async (type: number, count: number, value: Uint8Array) => {
    if (type !== 2 || count === 0) return "";
    const n = Math.min(count, 256);
    let held: Uint8Array;
    if (count <= 4) held = value.subarray(0, n);
    else {
      const at = u32(value, 0);
      if (at > room || room - at < n) return "";
      held = await read(base + at, n);
      if (held.length < n) return "";
    }
    let said = "";
    for (const c of held) {
      if (c === 0) break;
      if (c >= 0x20 && c < 0x7f) said += String.fromCharCode(c);
    }
    return said.trim();
  };
  // Each of a directory's entries: its tag, type, count and the four bytes that hold
  // its value or where it is; never more than fit before the end, or a camera writes.
  const entries = async (at: number) => {
    const found: { tag: number; type: number; count: number; value: Uint8Array }[] = [];
    if (at > room || room - at < 2) return found;
    const counted = await read(base + at, 2);
    if (counted.length < 2) return found;
    const count = Math.min(u16(counted, 0), Math.floor((room - at - 2) / 12), 1024);
    if (count === 0) return found;
    const all = await read(base + at + 2, count * 12);
    if (all.length < count * 12) return found;
    for (let i = 0; i < count; i++) {
      const e = all.subarray(i * 12, i * 12 + 12);
      found.push({ tag: u16(e, 0), type: u16(e, 2), count: u32(e, 4), value: e.subarray(8, 12) });
    }
    return found;
  };
  let exif: number | undefined;
  for (const e of await entries(u32(head, 4))) {
    if (e.tag === 0x010f) out.make = await text(e.type, e.count, e.value);
    else if (e.tag === 0x0110) out.model = await text(e.type, e.count, e.value);
    else if (e.tag === 0x0112 && e.type === 3 && e.count >= 1) {
      const turned = u16(e.value, 0);
      if (turned >= 1 && turned <= 8) out.orientation = turned;
    } else if (e.tag === 0x8769 && (e.type === 4 || e.type === 13) && e.count === 1) exif = u32(e.value, 0);
  }
  if (exif !== undefined) {
    for (const e of await entries(exif)) {
      if (e.tag === 0x9003) out.taken = await text(e.type, e.count, e.value);
      else if (e.tag === 0x9011) out.offset = await text(e.type, e.count, e.value);
    }
  }
  return out;
}

const big16 = (b: Uint8Array, at: number) => (b[at]! << 8) | b[at + 1]!;
const big32 = (b: Uint8Array, at: number) => b[at]! * 0x1000000 + ((b[at + 1]! << 16) | (b[at + 2]! << 8) | b[at + 3]!);
const little32 = (b: Uint8Array, at: number) => (b[at]! | (b[at + 1]! << 8) | (b[at + 2]! << 16)) + b[at + 3]! * 0x1000000;
const says = (b: Uint8Array, at: number, word: string) => [...word].every((c, i) => b[at + i] === c.charCodeAt(0));

// The ways a photo's EXIF is found, by what its file starts with.
async function foundIn(read: BytesAt, size: number): Promise<PhotoFacts> {
  const start = await read(0, 16);
  if (start.length >= 4 && ((says(start, 0, "II") && start[2] === 42 && start[3] === 0) || (says(start, 0, "MM") && start[2] === 0 && start[3] === 42))) return tiff(read, 0, size);
  // A JPEG's segments, one after another, until its picture starts: the APP1 that says
  // Exif is the one.
  if (start.length >= 2 && start[0] === 0xff && start[1] === 0xd8) {
    let at = 2;
    for (let segments = 0; segments < 1024 && at + 4 <= size; segments++) {
      const head = await read(at, 10);
      if (head.length < 4 || head[0] !== 0xff) break;
      const marker = head[1]!;
      if (marker === 0xff) {
        at++; // padding before a marker
        continue;
      }
      if (marker === 0xd9 || marker === 0xda) break;
      if (marker === 0x01 || (marker >= 0xd0 && marker <= 0xd7)) {
        at += 2;
        continue;
      }
      const length = big16(head, 2);
      if (length < 2) break;
      if (marker === 0xe1 && length >= 14 && head.length >= 10 && says(head, 4, "Exif\0\0")) return tiff(read, at + 10, Math.min(size, at + 2 + length));
      at += 2 + length;
    }
    return nothing();
  }
  // A PNG's chunks, until its end: eXIf's is the TIFF itself.
  if (start.length >= 8 && [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a].every((b, i) => start[i] === b)) {
    let at = 8;
    for (let chunks = 0; chunks < 65536 && at + 8 <= size; chunks++) {
      const head = await read(at, 8);
      if (head.length < 8) break;
      const length = big32(head, 0);
      if (says(head, 4, "eXIf")) return tiff(read, at + 8, Math.min(size, at + 8 + length));
      if (says(head, 4, "IEND")) break;
      at += 12 + length;
    }
    return nothing();
  }
  // A WebP's chunks, in its RIFF: EXIF's is the TIFF, sometimes after the words Exif
  // and two NULs, as a JPEG's is.
  if (start.length >= 12 && says(start, 0, "RIFF") && says(start, 8, "WEBP")) {
    const end = Math.min(size, 8 + little32(start, 4));
    let at = 12;
    for (let chunks = 0; chunks < 65536 && at + 8 <= end; chunks++) {
      const head = await read(at, 14);
      if (head.length < 8) break;
      const length = little32(head, 4);
      if (says(head, 0, "EXIF")) {
        let from = at + 8;
        const to = Math.min(end, from + length);
        if (to - from >= 6 && head.length >= 14 && says(head, 8, "Exif\0\0")) from += 6;
        return tiff(read, from, to);
      }
      at += 8 + length + (length & 1);
    }
  }
  return nothing();
}

// Read from a picture's bytes, or from a file, reading only the little that's needed.
export function factsIn(bytes: Uint8Array): Promise<PhotoFacts> {
  return foundIn(async (at, n) => bytes.subarray(Math.min(at, bytes.length), Math.min(at + n, bytes.length)), bytes.length);
}
export async function factsOf(file: Blob): Promise<PhotoFacts> {
  try {
    return await foundIn(async (at, n) => new Uint8Array(await bytesOf(file.slice(at, at + n))), file.size);
  } catch {
    return nothing();
  }
}

// A file's bytes, as every browser reads them.
function bytesOf(file: Blob): Promise<ArrayBuffer> {
  if (typeof file.arrayBuffer === "function") return file.arrayBuffer();
  return new Promise((done, failed) => {
    const reader = new FileReader();
    reader.onload = () => done(reader.result as ArrayBuffer);
    reader.onerror = () => failed(reader.error);
    reader.readAsArrayBuffer(file);
  });
}

// Its make and model together, the make said once when the model starts with it, as
// Canon's Canon EOS R5 does: what a person calls the camera.
export function cameraOf(f: PhotoFacts): string {
  if (!f.model) return f.make;
  if (!f.make) return f.model;
  const first = f.make.split(" ")[0]!;
  return f.model.toLowerCase().startsWith(first.toLowerCase()) ? f.model : `${f.make} ${f.model}`;
}

// When it was taken, as a date is kept, like 2026-10-12T06:30:15Z: at its offset, or
// else as the time is where this runs. Nothing when it doesn't say, or says wrong.
export function takenAt(f: PhotoFacts): string | undefined {
  const m = /^(\d{4}):(\d{2}):(\d{2}) (\d{2}):(\d{2}):(\d{2})/.exec(f.taken);
  if (!m) return undefined;
  const [year, month, day, hour, minute, second] = m.slice(1).map(Number) as [number, number, number, number, number, number];
  if (year < 1800 || month < 1 || month > 12 || day < 1 || day > new Date(Date.UTC(year, month, 0)).getUTCDate() || hour > 23 || minute > 59 || second > 60) return undefined;
  const s = Math.min(second, 59);
  const o = /^([+-])(\d{2}):(\d{2})/.exec(f.offset);
  const at = o
    ? Date.UTC(year, month - 1, day, hour, minute, s) - (o[1] === "-" ? -1 : 1) * (Number(o[2]) * 60 + Number(o[3])) * 60000
    : new Date(year, month - 1, day, hour, minute, s).getTime();
  return new Date(at).toISOString().replace(".000Z", "Z");
}
