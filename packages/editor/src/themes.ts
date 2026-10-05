// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The color themes code can be shown in: the most used ones, each with its light and
// dark side where it has both, so it follows the page; a theme with one side is that
// side on either. All come with shiki but PaperColor, whose palette, from its Vim
// theme, is written out here for the kinds of words .one has. A theme is loaded the
// first time it's chosen.

import type { ThemeRegistration } from "shiki/core";

export interface CodeTheme {
  name: string; // as it's offered, like One Dark Pro
  light: string; // the theme shown on a light page, by its id
  dark: string; // and on a dark one
  load: () => Promise<ThemeRegistration[]>;
}

const shiki = (...loads: (() => Promise<{ default: unknown }>)[]) => async () =>
  (await Promise.all(loads.map((load) => load()))).map((m) => m.default as ThemeRegistration);

export const codeThemes: CodeTheme[] = [
  { name: "GitHub", light: "github-light", dark: "github-dark", load: shiki(() => import("@shikijs/themes/github-light"), () => import("@shikijs/themes/github-dark")) },
  { name: "One Dark Pro", light: "one-light", dark: "one-dark-pro", load: shiki(() => import("@shikijs/themes/one-light"), () => import("@shikijs/themes/one-dark-pro")) },
  { name: "Dracula", light: "dracula", dark: "dracula", load: shiki(() => import("@shikijs/themes/dracula")) },
  { name: "Catppuccin", light: "catppuccin-latte", dark: "catppuccin-mocha", load: shiki(() => import("@shikijs/themes/catppuccin-latte"), () => import("@shikijs/themes/catppuccin-mocha")) },
  { name: "Tokyo Night", light: "tokyo-night", dark: "tokyo-night", load: shiki(() => import("@shikijs/themes/tokyo-night")) },
  { name: "Nord", light: "nord", dark: "nord", load: shiki(() => import("@shikijs/themes/nord")) },
  { name: "PaperColor", light: "papercolor-light", dark: "papercolor-dark", load: async () => [paperColor("light"), paperColor("dark")] },
  { name: "Solarized", light: "solarized-light", dark: "solarized-dark", load: shiki(() => import("@shikijs/themes/solarized-light"), () => import("@shikijs/themes/solarized-dark")) },
  { name: "Gruvbox", light: "gruvbox-light-medium", dark: "gruvbox-dark-medium", load: shiki(() => import("@shikijs/themes/gruvbox-light-medium"), () => import("@shikijs/themes/gruvbox-dark-medium")) },
  { name: "Monokai", light: "monokai", dark: "monokai", load: shiki(() => import("@shikijs/themes/monokai")) },
];

export const defaultTheme = codeThemes[0];

export function codeTheme(name: string | undefined): CodeTheme {
  return codeThemes.find((t) => t.name === name) ?? defaultTheme;
}

// PaperColor, by Nikyle Nguyen (MIT), as its Vim theme colors each kind of word.
function paperColor(side: "light" | "dark"): ThemeRegistration {
  const c =
    side === "light"
      ? { bg: "#eeeeee", fg: "#444444", comment: "#878787", keyword: "#d70087", name: "#0087af", type: "#005faf", user: "#008700", string: "#5f8700", number: "#d75f00", modifier: "#8700af", field: "#005f87", operator: "#0087af" }
      : { bg: "#1c1c1c", fg: "#d0d0d0", comment: "#808080", keyword: "#ff5faf", name: "#5fafd7", type: "#00afaf", user: "#5faf00", string: "#d7af5f", number: "#ff8700", modifier: "#af87d7", field: "#87afd7", operator: "#5fafd7" };
  const rule = (scope: string[], foreground: string, fontStyle?: string) => ({ scope, settings: { foreground, ...(fontStyle ? { fontStyle } : {}) } });
  return {
    name: `papercolor-${side}`,
    type: side,
    colors: { "editor.background": c.bg, "editor.foreground": c.fg },
    tokenColors: [
      rule(["comment"], c.comment, "italic"),
      rule(["storage.type", "keyword.control", "keyword.other"], c.keyword),
      rule(["entity.name.type", "entity.name.function", "entity.name.namespace"], c.name),
      rule(["support.type"], c.type),
      rule(["support.type.user"], c.user),
      rule(["variable.other.field", "variable.parameter"], c.field),
      rule(["storage.modifier"], c.modifier),
      rule(["string", "string.other.path", "string.regexp"], c.string),
      rule(["constant.numeric", "constant.language", "constant.other", "constant.character.escape"], c.number),
      rule(["keyword.operator", "punctuation.separator.namespace"], c.operator),
    ],
  };
}
