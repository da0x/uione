// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The color themes code can be shown in, in two lists: themes for a light page and
// themes for a dark one. A reader chooses one of each, and the code is shown in
// whichever suits the page now, so switching the page's mode switches the theme
// back to the one chosen for it. All come with shiki but PaperColor, whose palette,
// from its Vim theme, is written out here for the kinds of words .one has. A theme
// is loaded the first time it's shown.

import type { ThemeRegistration } from "shiki/core";

export interface CodeTheme {
  name: string; // as it's offered, like One Dark Pro
  id: string; // shiki's name for it
  dark: boolean; // for a dark page, or a light one
  load: () => Promise<ThemeRegistration>;
}

const shiki = (load: () => Promise<{ default: unknown }>) => async () => (await load()).default as ThemeRegistration;

export const lightThemes: CodeTheme[] = [
  { name: "GitHub Light", id: "github-light", dark: false, load: shiki(() => import("@shikijs/themes/github-light")) },
  { name: "One Light", id: "one-light", dark: false, load: shiki(() => import("@shikijs/themes/one-light")) },
  { name: "Catppuccin Latte", id: "catppuccin-latte", dark: false, load: shiki(() => import("@shikijs/themes/catppuccin-latte")) },
  { name: "Solarized Light", id: "solarized-light", dark: false, load: shiki(() => import("@shikijs/themes/solarized-light")) },
  { name: "Gruvbox Light", id: "gruvbox-light-medium", dark: false, load: shiki(() => import("@shikijs/themes/gruvbox-light-medium")) },
  { name: "PaperColor Light", id: "papercolor-light", dark: false, load: async () => paperColor("light") },
];

export const darkThemes: CodeTheme[] = [
  { name: "GitHub Dark", id: "github-dark", dark: true, load: shiki(() => import("@shikijs/themes/github-dark")) },
  { name: "One Dark Pro", id: "one-dark-pro", dark: true, load: shiki(() => import("@shikijs/themes/one-dark-pro")) },
  { name: "Dracula", id: "dracula", dark: true, load: shiki(() => import("@shikijs/themes/dracula")) },
  { name: "Catppuccin Mocha", id: "catppuccin-mocha", dark: true, load: shiki(() => import("@shikijs/themes/catppuccin-mocha")) },
  { name: "Tokyo Night", id: "tokyo-night", dark: true, load: shiki(() => import("@shikijs/themes/tokyo-night")) },
  { name: "Nord", id: "nord", dark: true, load: nord },
  { name: "PaperColor Dark", id: "papercolor-dark", dark: true, load: async () => paperColor("dark") },
  { name: "Solarized Dark", id: "solarized-dark", dark: true, load: shiki(() => import("@shikijs/themes/solarized-dark")) },
  { name: "Gruvbox Dark", id: "gruvbox-dark-medium", dark: true, load: shiki(() => import("@shikijs/themes/gruvbox-dark-medium")) },
  { name: "Monokai", id: "monokai", dark: true, load: shiki(() => import("@shikijs/themes/monokai")) },
];

// The themes code is shown in: one for a light page and one for a dark one.
export interface Themes {
  light: CodeTheme;
  dark: CodeTheme;
}

export const defaultThemes: Themes = { light: lightThemes[0], dark: darkThemes[0] };

// The themes chosen, by name, each of its own list, or the default for its page.
export function themesOf(light?: string, dark?: string): Themes {
  return {
    light: lightThemes.find((t) => t.name === light) ?? defaultThemes.light,
    dark: darkThemes.find((t) => t.name === dark) ?? defaultThemes.dark,
  };
}

// What a single choice of earlier versions, one name for both pages, becomes.
export function fromOneChoice(name: string | undefined): { light?: string; dark?: string } {
  switch (name) {
    case "GitHub":
      return {};
    case "One Dark Pro":
      return { light: "One Light", dark: "One Dark Pro" };
    case "Catppuccin":
      return { light: "Catppuccin Latte", dark: "Catppuccin Mocha" };
    case "Solarized":
      return { light: "Solarized Light", dark: "Solarized Dark" };
    case "Gruvbox":
      return { light: "Gruvbox Light", dark: "Gruvbox Dark" };
    case "PaperColor":
      return { light: "PaperColor Light", dark: "PaperColor Dark" };
    case "Dracula":
    case "Tokyo Night":
    case "Nord":
    case "Monokai":
      return { dark: name };
    default:
      return {};
  }
}

// Nord on a darker ground: its own blue-gray, hue and saturation kept, as dark as
// the pages it's shown on, so the code doesn't sit on a lighter slab of gray.
async function nord(): Promise<ThemeRegistration> {
  const theme = (await import("@shikijs/themes/nord")).default as ThemeRegistration;
  const ground = "#1e2229";
  return {
    ...theme,
    bg: ground,
    colors: { ...theme.colors, "editor.background": ground, "editorGutter.background": ground, "panel.background": ground, "sideBar.background": ground },
  };
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
