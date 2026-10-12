# loom

A modern C++ toolkit for Wayland, drawing with Vulkan. Apache-2.0.

It's written in C++26 on the system's own libraries: Wayland, Vulkan, HarfBuzz,
FreeType, fontconfig and xkbcommon. Nothing is downloaded while it builds.

```sh
cmake -S . -B build && cmake --build build
build/hello
```

Building needs g++ 16, CMake, `wayland-protocols`, and `vulkan-headers`.
