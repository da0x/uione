// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Photos made for tests, as a camera would write them: a JPEG, PNG or WebP with an
// EXIF block in it, the block saying when it was taken, with what, and which way up it
// was held.

export interface Said {
  little?: boolean;
  orientation?: number;
  make?: string;
  model?: string;
  taken?: string;
  offset?: string;
}

// A TIFF's structure as EXIF is: its first directory, with the camera and which way up,
// pointing to the EXIF directory, with when.
export function exif(s: Said): Uint8Array<ArrayBuffer> {
  const little = s.little ?? true;
  const out: number[] = [];
  const u16 = (at: number, v: number) => {
    out[at + (little ? 0 : 1)] = v & 0xff;
    out[at + (little ? 1 : 0)] = (v >> 8) & 0xff;
  };
  const u32 = (at: number, v: number) => {
    for (let i = 0; i < 4; i++) out[at + (little ? i : 3 - i)] = (v >>> (8 * i)) & 0xff;
  };
  type Entry = { tag: number; type: number; text?: string; number?: number };
  const directory = (at: number, entries: Entry[]) => {
    u16(at, entries.length);
    let data = at + 2 + entries.length * 12 + 4;
    entries.forEach((e, i) => {
      const here = at + 2 + i * 12;
      u16(here, e.tag);
      u16(here + 2, e.type);
      if (e.type === 2) {
        const bytes = [...(e.text ?? "")].map((c) => c.charCodeAt(0)).concat(0);
        u32(here + 4, bytes.length);
        if (bytes.length <= 4) {
          u32(here + 8, 0);
          bytes.forEach((b, c) => (out[here + 8 + c] = b));
        } else {
          u32(here + 8, data);
          bytes.forEach((b, c) => (out[data + c] = b));
          data += bytes.length + (bytes.length & 1);
        }
      } else {
        u32(here + 4, 1);
        if (e.type === 3) u16(here + 8, e.number ?? 0), u16(here + 10, 0);
        else u32(here + 8, e.number ?? 0);
      }
    });
    u32(at + 2 + entries.length * 12, 0);
    for (let i = 0; i < data; i++) out[i] ??= 0;
    return data;
  };
  out.push(little ? 0x49 : 0x4d, little ? 0x49 : 0x4d);
  u16(2, 42);
  u32(4, 8);
  const first: Entry[] = [], later: Entry[] = [];
  if (s.make) first.push({ tag: 0x010f, type: 2, text: s.make });
  if (s.model) first.push({ tag: 0x0110, type: 2, text: s.model });
  if (s.orientation) first.push({ tag: 0x0112, type: 3, number: s.orientation });
  if (s.taken) later.push({ tag: 0x9003, type: 2, text: s.taken });
  if (s.offset) later.push({ tag: 0x9011, type: 2, text: s.offset });
  if (later.length) first.push({ tag: 0x8769, type: 4, number: 0 });
  const after = directory(8, first);
  if (later.length) {
    first[first.length - 1]!.number = after;
    directory(8, first);
    directory(after, later);
  }
  return Uint8Array.from(out);
}

export const words = (w: string) => [...w].map((c) => c.charCodeAt(0));
const big = (n: number, bytes: number) => Array.from({ length: bytes }, (_, i) => (n >>> (8 * (bytes - 1 - i))) & 0xff);
const littleOf = (n: number) => [0, 1, 2, 3].map((i) => (n >>> (8 * i)) & 0xff);

// A JPEG's start, its EXIF in an APP1 after a JFIF APP0, then its picture starting,
// which needn't be a real one: nothing past where the picture starts is read.
export function jpeg(block: Uint8Array): Uint8Array<ArrayBuffer> {
  const app0 = [0xff, 0xe0, ...big(16, 2), ...words("JFIF"), 0, 1, 1, 0, 0, 1, 0, 1, 0, 0];
  const app1 = [0xff, 0xe1, ...big(block.length + 8, 2), ...words("Exif"), 0, 0, ...block];
  return Uint8Array.from([0xff, 0xd8, ...app0, 0xff, 0xff, ...app1, 0xff, 0xda, 0, 2, 1, 2, 3, 0xff, 0xd9]);
}

// A PNG's signature, a size chunk, its EXIF in an eXIf chunk, and its end.
export function png(block: Uint8Array): Uint8Array<ArrayBuffer> {
  const chunk = (type: string, data: number[]) => [...big(data.length, 4), ...words(type), ...data, 0, 0, 0, 0];
  return Uint8Array.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, ...chunk("IHDR", [0, 0, 0, 20, 0, 0, 0, 40, 8, 6, 0, 0, 0]), ...chunk("eXIf", [...block]), ...chunk("IEND", [])]);
}

// A WebP's RIFF, its EXIF chunk after an odd-sized one, after the words Exif and two
// NULs when `prefixed`.
export function webp(block: Uint8Array, prefixed: boolean): Uint8Array<ArrayBuffer> {
  const data = [...(prefixed ? [...words("Exif"), 0, 0] : []), ...block];
  const body = [...words("WEBP"), ...words("ICCP"), ...littleOf(3), 1, 2, 3, 0, ...words("EXIF"), ...littleOf(data.length), ...data, ...(data.length & 1 ? [0] : [])];
  return Uint8Array.from([...words("RIFF"), ...littleOf(body.length), ...body]);
}
