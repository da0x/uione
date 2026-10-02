# @uione/radix

The styled component set for uione apps, written for uione on Radix primitives and
Tailwind. It only draws: routing, data and commands come from `@uione/react`.

## Install

```sh
npm install @uione/radix @uione/react react react-dom
```

`@uione/react`, `react` and `react-dom` are peer dependencies.

## Use

```tsx
import { App } from "@uione/react";
import { radix } from "@uione/radix";
import "@uione/radix/styles.css";

<App name="library" screens={screens} ui={radix} data={source} />;
```

`styles.css` is prebuilt. It includes Tailwind's base reset, so importing it restyles
the whole page, and the color variables the components use (`--color-page`,
`--color-ink`, `--color-accent` and the rest), with dark values under
`prefers-color-scheme: dark`.

## Bundlers

The code highlighter imports its grammar as JSON with an import attribute
(`import grammar from "./grammar/uione.json" with { type: "json" }`), so the app's
bundler has to support import attributes: Vite or Rollup 4, esbuild 0.19.7 or later,
or a recent webpack 5.

## License

LGPL-3.0-only. See `LICENSE`, and `COPYING` for the GNU GPL it builds on.

The uione TextMate grammar shipped in `dist/grammar/uione.json` is also offered under
LGPL-3.0-only here, by its author, though its source in the uione repository's
`editors/` folder is AGPL-3.0-only.
