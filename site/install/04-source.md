# From source

Building `one` yourself works on any system with Git, CMake 3.25 or newer, and a
C++23 compiler, such as GCC 14 or a recent Clang:

```sh
git clone https://github.com/da0x/uione.git
cd uione
git checkout v0.6.4
make -C compiler
```

Leave out `git checkout` to build `main`, which has what's next but isn't a
release. The compiler is `compiler/build/one`. Put it on your path:

```sh
install compiler/build/one ~/.local/bin/one
one --version
```

`make -C compiler test` builds it and runs its tests as well.

## The browser build

The same compiler builds to WebAssembly with Emscripten, in Docker, for the studio
and `@uione/compiler`:

```sh
tools/wasm/build
```
