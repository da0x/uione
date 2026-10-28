// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// SVG, as pictures come in: known by what it is, and sized from what it says, as the
// desktop knows and sizes one by the pinned nanosvg. It's never read as pixels.

// An SVG's text, when a file or what's fetched is one: said so by its type or its name,
// or starting with a tag and holding an <svg>.
export async function svgIn(file: Blob): Promise<string | undefined> {
  const named = file.type === "image/svg+xml" || (file instanceof File && file.name.toLowerCase().endsWith(".svg"));
  if (!named && file.type.startsWith("image/") && file.type !== "image/svg+xml") return undefined;
  // Its start first, so a large picture isn't read as words to learn it isn't one.
  const start = (await textOf(file.slice(0, 1024))).replace(/^\uFEFF/, "").trimStart();
  if (!start.startsWith("<")) return undefined;
  const text = await textOf(file);
  return text.includes("<svg") ? text : undefined;
}

// A file's text, as every browser reads it.
function textOf(file: Blob): Promise<string> {
  if (typeof file.text === "function") return file.text();
  return new Promise((done, failed) => {
    const reader = new FileReader();
    reader.onload = () => done(String(reader.result));
    reader.onerror = () => failed(reader.error);
    reader.readAsText(file);
  });
}

// An SVG's own size: its width and height, or its viewBox's when it says none.
export function svgSize(svg: string): { w: number; h: number } | undefined {
  const root = new DOMParser().parseFromString(svg, "image/svg+xml").documentElement;
  if (root.nodeName !== "svg") return undefined;
  const box = (root.getAttribute("viewBox") ?? "").split(/[\s,]+/).map(Number);
  // A length said in percent is of whatever it's in, so it says nothing of its own size.
  const length = (said: string | null) => (said && !said.trim().endsWith("%") ? parseFloat(said) : NaN);
  const w = length(root.getAttribute("width")) || box[2] || 0, h = length(root.getAttribute("height")) || box[3] || 0;
  return w > 0 && h > 0 ? { w, h } : undefined;
}

// A picture's own size, an SVG's as it says and any other's as it's read; nothing when
// it isn't one that can be read.
export async function pictureSize(file: Blob): Promise<{ w: number; h: number } | undefined> {
  const svg = await svgIn(file);
  if (svg !== undefined) return svgSize(svg);
  try {
    const bitmap = await createImageBitmap(file);
    const size = { w: bitmap.width, h: bitmap.height };
    bitmap.close();
    return size;
  } catch {
    return undefined;
  }
}
