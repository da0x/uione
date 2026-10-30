# one::syntax

Code colored as it's read, the same in a window and on a web page.

```
#include <vector>                      directive
int main() { return 0; }  // done      type, function, keyword, number, comment
```

It's a library with one table of languages and two implementations, C++ and
TypeScript, held to one shared file of cases, so a code block looks alike on every
target. It doesn't run a regular-expression engine or a grammar: a language is its
comments, its quotes and its words, and that's enough to color most code well.

## Languages

C, C++, C#, Java, JavaScript, TypeScript, Rust, Go, Swift, Kotlin, Python, Ruby,
Shell, SQL, JSON, YAML and Lua. Each is found by its name or another it goes by, in
any case: `cpp`, `c++` and `hpp` are all C++.

A language is one line of `tests/languages.tsv`: its name, what it's called, its other
names, its comments, its quotes, its strings over several lines, what it does besides,
like a `#` starting a line being a directive, and its keywords, types and constants.
To add one, add its line and its cases, then copy the table into both
implementations:

```sh
node syntax/write-table.mjs
```

Each implementation's test fails while its copy is behind.

## What's colored

Keywords, types, constants, strings, numbers, comments, directives like `#include` or
`@Override`, and a function's name where it's called. Each kind has one color on a
light page and one on a dark one, GitHub's, so code reads as it does there.

## Using it

- C++: `#include <one/syntax.hpp>`, then `one::syntax::find("cpp")` and
  `one::syntax::highlight(source, *language)`, spans in bytes.
- TypeScript: `findLanguage` and `highlight` from `@uione/react`'s `syntax.ts`, spans in
  a string's own indices.
