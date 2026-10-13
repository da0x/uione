// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A hand-written component: anything the language doesn't say, drawn where a screen
// names it. This is the desktop's and the terminal's, a function of the page giving
// a view, written with the same parts their screens are; welcome_note.tsx is the
// web's.

#include "../app.hpp"

namespace app {

	view welcome_note(page&) { return text({words("This note is a component, written by hand: in C++ in the desktop and terminal apps.")}); }

}  // namespace app
