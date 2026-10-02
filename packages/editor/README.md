# @uione/editor

An editor for `.one` files, on CodeMirror 6:

- highlighting from the same grammar as uione's VS Code and Vim support, in light and
  dark;
- the compiler's problems underlined as you type, from `@uione/compiler`;
- tabs, one per level, shown as wide as the reader likes;
- beside it, the code the project becomes, with every line the cursor's line made
  marked.

```tsx
import { createCompiler } from "@uione/compiler";
import { Workbench } from "@uione/editor";

const compiler = createCompiler();

<Workbench compiler={compiler} files={files} path="main.one" onChange={(path, text) => save(path, text)} tabWidth={4} />;
```

`Editor` and `Generated` are the two halves on their own, and `highlighting`,
`problems` and `tabs` are plain CodeMirror extensions for an editor of your own.
The components carry class names (`uione-editor`, `uione-generated`,
`uione-from-here`, `uione-line-number`) and no styles of their own.

## License

AGPL-3.0-only (`LICENSE`).
