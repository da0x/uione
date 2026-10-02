# @uione/compiler

The uione compiler, `one`, built for the browser with Emscripten and run in a Web
Worker, so a page can check a project as it's typed, build it, and show what each
line of a `.one` file becomes. Its output is the same, byte for byte, as the native
compiler's.

```ts
import { createCompiler } from "@uione/compiler";

const compiler = createCompiler();
const files = { "main.one": "namespace tasks { ... }" };

const { problems } = await compiler.check(files);
const built = await compiler.build(files); // every generated file, with sources
const shown = await compiler.show(files, "main.one", 12); // what line 12 becomes
```

- Files are given by their path within the project, like `main.one` or
  `docs/intro.md`, and problems and sources name them the same way.
- Each generated line's source is the `.one` line it came from, `"same"` for a line
  every project gets, or `null` for a blank line.
- The worker is `@uione/compiler/worker`, and loads `one.wasm` from beside it. A
  bundler that understands `new URL("./worker.js", import.meta.url)` and module
  workers, like Vite, packages both.

## License

AGPL-3.0-only (`LICENSE`), like the compiler it's built from. Code the compiler
generates belongs to whoever generated it.
