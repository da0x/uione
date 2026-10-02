# 0003: The compiler is written in C++23

- Status: Accepted
- Date: 2026-10-02
- Deciders: Daher Alfawares

## Context

The compiler has to run on the command line and in the browser, where the studio
recompiles a project as it's typed. Go compiled to WebAssembly is several
megabytes; C++ through Emscripten is small and fast.

## Decision

The compiler is C++23, in its maintainer's style: snake_case, namespaces like
`one::language` and `one::generators`, a hand-written lexer and parser, and every
syntax node keeping its file, line and column. Generators write through
`code::stream`, which tracks indentation and the `.one` line each output line came
from. It builds with CMake and is tested with doctest and golden files.

Nothing that touches the operating system lives outside `compiler/platform/`, and
there are no threads, so the same code builds for the browser. An Emscripten build
has been tried: 885 KB, and output identical to the native build.

## Consequences

The studio runs the same compiler as the command line.

Releases are needed for Linux, macOS, Windows and WebAssembly. Fewer web
developers write C++, so fewer can work on the compiler itself; the libraries,
examples and editor support stay in the languages their users know.
