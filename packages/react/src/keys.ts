// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// The id of an entity whose key has several parts, like a project keyed by its
// owner and its name, made exactly as the one library makes it: each part escaped
// as a path segment, a dash in any part but the first escaped too, and the parts
// joined by dashes. So da0x and neotrac are da0x-neotrac, and an address can name
// a project by its parts, /da0x/neotrac, rather than by its id.

// Go's url.PathEscape, which leaves $&+:=@ as they are and escapes !'()*.
function pathEscape(value: string): string {
  return encodeURIComponent(value)
    .replace(/%(24|26|2B|3A|3D|40)/g, (_, hex: string) => String.fromCharCode(parseInt(hex, 16)))
    .replace(/[!'()*]/g, (c) => "%" + c.charCodeAt(0).toString(16).toUpperCase());
}

// The id the parts make, or undefined while any part is missing.
export function keyOf(parts: readonly (string | undefined)[]): string | undefined {
  if (parts.some((part) => part === undefined || part.trim() === "")) return undefined;
  return parts
    .map((part, i) => {
      const escaped = pathEscape((part as string).trim());
      return i === 0 ? escaped : escaped.replaceAll("-", "%2D");
    })
    .join("-");
}

// The parts an id was made from, given how many there are. Only the first part
// can hold a dash, so the parts are split at the last dashes.
export function partsOf(id: string, count: number): string[] | undefined {
  const cuts: number[] = [];
  for (let at = id.length - 1; at >= 0 && cuts.length < count - 1; at--) {
    if (id[at] === "-") cuts.unshift(at);
  }
  if (cuts.length !== count - 1) return undefined;
  const parts: string[] = [];
  let from = 0;
  for (const cut of cuts) {
    parts.push(id.slice(from, cut));
    from = cut + 1;
  }
  parts.push(id.slice(from));
  try {
    return parts.map((part) => decodeURIComponent(part));
  } catch {
    return undefined;
  }
}
