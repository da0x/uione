# one::desktop

uione's toolkit for desktop apps: modern C++ on Wayland, drawing with Vulkan.
Apache-2.0, so any program may use it.

It's written in C++26 on the system's own libraries: Wayland, Vulkan, HarfBuzz,
FreeType, fontconfig and xkbcommon. Nothing is downloaded while it builds.

```cpp
namespace ui = one::desktop;

ui::state<std::vector<issue>> issues = load();
window.show([=] {
	return ui::column(
		ui::text("Issues", ui::current_theme().heading()),
		ui::scroll(ui::list(*issues, &issue::id, [](const issue& i) {
			return ui::row(ui::edge(urgency(i)), ui::text(i.title).grow()).spacing(10);
		}, open))
	).spacing(16).padding(24);
});
```

A view is C++ that's built again whenever a state it read changes. What's built is
matched with what was there before, so the chosen row, the scroll and the focus stay
where they were.

```sh
cmake -S desktop -B desktop/build && cmake --build desktop/build
desktop/build/demo     # issues in a list, changing as you watch
desktop/build/tests    # layout, state, lists, and scenes drawn offscreen against tests/golden
```

Building needs g++ 16, CMake, `wayland-protocols`, `vulkan-headers`, `shaderc`
(for glslc), HarfBuzz, FreeType, fontconfig and xkbcommon. The tests draw on the GPU,
or on Mesa's software Vulkan (lavapipe) where there's none.

It began as libloom, whose history this directory keeps.
