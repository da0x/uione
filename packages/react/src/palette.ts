// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A theme made at random from a palette, as one::screens' palette.hpp makes it, line
// for line, so a seed is the same theme in every app: screens/tests/golden/palettes.txt
// holds both to it. A seed picks a hue and how vivid it is, a scheme the hues around
// it, and every role comes from them, light and dark. Danger, warning and success
// stay red, amber and green, each pulled a little toward the palette's hue. Every
// color then reads on what it sits on, as the compiler checks a theme, so the theme
// written out is one a project can say as it is.

export const schemes = ["complementary", "analogous", "triadic", "split_complementary", "monochrome"] as const;
export type Scheme = (typeof schemes)[number];

export const roles = ["page", "surface", "sunken", "ink", "muted", "line", "accent", "danger", "success", "warning"] as const;
export type Role = (typeof roles)[number];

export interface PaletteTheme {
  seed: number;
  scheme: Scheme;
  colors: Record<Role, { light: string; dark: string }>;
}

// Colors as a theme writes them, #rrggbb, measured as WCAG does, moved in OKLab.
function rgb(hex: string): [number, number, number] {
  const channel = (at: number) => parseInt(hex.slice(at, at + 2), 16) / 255;
  return [channel(1), channel(3), channel(5)];
}
const linear = (v: number) => (v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4));
const gamma = (v: number) => (v <= 0.0031308 ? 12.92 * v : 1.055 * Math.pow(v, 1 / 2.4) - 0.055);

function luminance(hex: string): number {
  const [r, g, b] = rgb(hex);
  return 0.2126 * linear(r) + 0.7152 * linear(g) + 0.0722 * linear(b);
}

// How much one color stands out from another: 21 for black on white, 1 for the same.
export function contrast(a: string, b: string): number {
  const x = luminance(a);
  const y = luminance(b);
  return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05);
}

function oklab(hex: string): [number, number, number] {
  const [r0, g0, b0] = rgb(hex);
  const r = linear(r0);
  const g = linear(g0);
  const b = linear(b0);
  const l = Math.cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
  const m = Math.cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
  const s = Math.cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
  return [
    0.2104542553 * l + 0.793617785 * m - 0.0040720468 * s,
    1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s,
    0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s,
  ];
}

function toRgb([L, a, b]: [number, number, number]): [number, number, number] {
  const l = Math.pow(L + 0.3963377774 * a + 0.2158037573 * b, 3);
  const m = Math.pow(L - 0.1055613458 * a - 0.0638541728 * b, 3);
  const s = Math.pow(L - 0.0894841775 * a - 1.291485548 * b, 3);
  return [
    4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
    -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
    -0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s,
  ];
}

const clamp = (v: number, low: number, high: number) => Math.min(Math.max(v, low), high);

function hex(lab: [number, number, number]): string {
  const byte = (v: number) => Math.round(clamp(gamma(clamp(v, 0, 1)), 0, 1) * 255);
  return "#" + toRgb(lab).map((v) => byte(v).toString(16).padStart(2, "0")).join("");
}

// Whether a ground is dark, so what reads on it is light.
const darkGround = (color: string) => luminance(color) < 0.18;

// The nearest color to one that reads on another at 4.5:1: darker on a light
// ground, lighter on a dark one. Empty when no lightness reaches it.
function nearest(color: string, ground: string): string {
  const lab = oklab(color);
  const step = luminance(ground) > 0.18 ? -0.005 : 0.005;
  for (let i = 0; i < 200; i++) {
    const tried = hex(lab);
    if (contrast(tried, ground) >= 4.5) return tried;
    lab[0] += step;
    if (lab[0] < 0 || lab[0] > 1) break;
  }
  return "";
}

// Text on the accent, as on a button: white, or the page's own color, whichever reads better.
export function on(accent: string, page: string): string {
  return contrast("#ffffff", accent) >= contrast(page, accent) ? "#ffffff" : page;
}

// Numbers from a seed, the same in C++ and JavaScript: mulberry32.
function numbers(seed: number): () => number {
  let state = seed >>> 0;
  return () => {
    state = (state + 0x6d2b79f5) >>> 0;
    let t = state;
    t = Math.imul(t ^ (t >>> 15), t | 1) >>> 0;
    t = (t ^ (t + Math.imul(t ^ (t >>> 7), t | 61))) >>> 0;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function around(scheme: Scheme): number[] {
  if (scheme === "complementary") return [0, 180];
  if (scheme === "analogous") return [0, 30, -30];
  if (scheme === "triadic") return [0, 120, 240];
  if (scheme === "split_complementary") return [0, 150, 210];
  return [0];
}

// Whether a color in OKLab is one a screen can show, before it's rounded.
const shown = (lab: [number, number, number]) => toRgb(lab).every((v) => v >= -0.0001 && v <= 1.0001);

// A color by lightness, chroma and hue in degrees, made less vivid until a screen can show it.
function oklch(L: number, C: number, hue: number): string {
  const radians = (hue * Math.PI) / 180;
  for (let i = 0; i < 60 && !shown([L, C * Math.cos(radians), C * Math.sin(radians)]); i++) C *= 0.92;
  return hex([L, C * Math.cos(radians), C * Math.sin(radians)]);
}

// A hue moved toward another by a share of the way, no further than most degrees.
function toward(from: number, to: number, share: number, most: number): number {
  const apart = ((to - from + 540) % 360) - 180;
  return from + clamp(apart * share, -most, most);
}

// A color that reads at 4.5:1 on each ground, by lightness alone.
function reads(color: string, grounds: string[]): string {
  for (let round = 0; round < 3; round++) {
    let worst = grounds[0];
    for (const g of grounds) if (contrast(color, g) < contrast(color, worst)) worst = g;
    if (contrast(color, worst) >= 4.5) return color;
    const moved = nearest(color, worst);
    if (moved === "") return darkGround(worst) ? "#ffffff" : "#000000";
    color = moved;
  }
  return color;
}

// An accent that text reads on, white or the page's: darker on a light page, lighter
// on a dark one, still reading on the page and on panels.
function pressable(accent: string, page: string, surface: string): string {
  const lab = oklab(accent);
  const step = darkGround(page) ? 0.005 : -0.005;
  for (let i = 0; i < 200 && contrast(on(accent, page), accent) < 4.5; i++) {
    lab[0] = clamp(lab[0] + step, 0, 1);
    accent = hex(lab);
  }
  return reads(accent, [page, surface]);
}

// The theme a seed and a scheme make.
export function generate(seed: number, scheme: Scheme): PaletteTheme {
  const random = numbers(seed);
  const hue = random() * 360;
  const vivid = 0.09 + random() * 0.09;
  const tint = 0.008 + random() * 0.02;
  const offsets = around(scheme);
  const ground = hue + offsets[1 % offsets.length];
  const quiet = hue + offsets[offsets.length > 2 ? 2 : offsets.length - 1];
  const colors = {} as PaletteTheme["colors"];
  const set = (role: Role, light: string, dark: string) => (colors[role] = { light, dark });
  set("page", oklch(0.985, tint * 0.6, ground), oklch(0.16, tint * 1.2, ground));
  set("surface", oklch(0.997, tint * 0.3, ground), oklch(0.205, tint * 1.3, ground));
  set("sunken", oklch(0.95, tint, ground), oklch(0.13, tint * 1.1, ground));
  set("ink", oklch(0.22, Math.min(0.04, vivid * 0.25), ground), oklch(0.95, tint * 0.6, ground));
  set("muted", oklch(0.5, vivid * 0.25, quiet), oklch(0.74, vivid * 0.2, quiet));
  set("line", oklch(0.89, tint * 1.2, ground), oklch(0.32, tint * 1.4, ground));
  set("accent", oklch(0.52, vivid + 0.03, hue), oklch(0.76, vivid, hue));
  // Red, amber and green, leaning to the palette.
  const danger = toward(27, hue, 0.2, 14);
  const warning = toward(80, hue, 0.2, 12);
  const success = toward(150, hue, 0.2, 14);
  set("danger", oklch(0.54, clamp(vivid + 0.06, 0.13, 0.21), danger), oklch(0.72, clamp(vivid + 0.04, 0.12, 0.19), danger));
  set("warning", oklch(0.62, clamp(vivid + 0.04, 0.11, 0.17), warning), oklch(0.82, clamp(vivid + 0.02, 0.1, 0.16), warning));
  set("success", oklch(0.55, clamp(vivid + 0.02, 0.1, 0.17), success), oklch(0.76, clamp(vivid, 0.1, 0.16), success));
  // Each reads on what it sits on, as the compiler asks of a theme.
  for (const mode of ["light", "dark"] as const) {
    const page = colors.page[mode];
    const surface = colors.surface[mode];
    for (const role of ["ink", "muted"] as const) colors[role][mode] = reads(colors[role][mode], [page, surface]);
    colors.accent[mode] = pressable(colors.accent[mode], page, surface);
    for (const role of ["danger", "success", "warning"] as const) colors[role][mode] = reads(colors[role][mode], [surface]);
  }
  return { seed, scheme, colors };
}

// What a scheme is called: Split complementary.
export function schemeTitle(scheme: Scheme): string {
  const words = scheme.replace(/_/g, " ");
  return words.charAt(0).toUpperCase() + words.slice(1);
}

// The theme as a project says it, to paste into a .one file and name in its project
// block: theme triadic_48213.
export const nameOf = (theme: PaletteTheme) => `${theme.scheme}_${theme.seed}`;

export function written(theme: PaletteTheme): string {
  let out = `define theme ${nameOf(theme)} "${schemeTitle(theme.scheme)} ${theme.seed}" {\n`;
  for (const role of roles) out += `\t${role.padEnd(9)}${theme.colors[role].light}  dark ${theme.colors[role].dark}\n`;
  return out + "}\n";
}

// A seed picked at random, as one theme picks one.
export const randomSeed = () => 1 + Math.floor(Math.random() * 99999);
