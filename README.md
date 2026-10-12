# loom

A modern C++ toolkit for Wayland, drawing with Vulkan. Apache-2.0.

It's written in C++26 on the system's own libraries: Wayland, Vulkan, HarfBuzz,
FreeType, fontconfig and xkbcommon, drawn on the GPU with Vulkan 1.3. Nothing is downloaded while it builds.

```cpp
loom::state<std::vector<issue>> issues = load();
window.show([=] {
	return loom::column(
		loom::text("Issues", loom::current_theme().heading()),
		loom::scroll(loom::list(*issues, &issue::id, [](const issue& i) {
			return loom::row(loom::edge(urgency(i)), loom::text(i.title).grow()).spacing(10);
		}, open))
	).spacing(16).padding(24);
});
```

A view is C++ that's built again whenever a state it read changes. What's built is
matched with what was there before, so the chosen row, the scroll and the focus stay
where they were.

```sh
cmake -S . -B build && cmake --build build
build/demo     # issues in a list, changing as you watch
build/tests    # layout, state, lists, and scenes drawn offscreen against tests/golden
```

Building needs g++ 16, CMake, `wayland-protocols`, and `vulkan-headers`.
