# 0019: A collage is a picture made of layers, not a canvas

- Status: Accepted; its background superseded by [0021](0021-every-picture-in-a-collage-is-a-layer.md)
- Date: 2026-10-25
- Deciders: Daher Alfawares

## Context

A `canvas` shows a list's rows as cards on a surface without edges: it pans and
zooms, lays out cards not placed yet by their stage, keeps them to a grid, and is
the same list a table or a board shows, behind a switch.

A wallpaper, a poster or a slide is something else: a picture of a fixed size, in
its own pixels, made of layers. Its first picture is a background that fills it in
one of a few ways, and the rest are pictures cropped, turned and resized with their
shape kept, and words. It's drawn whole to a file, copied as a picture, and shown
small as a preview.

Making a canvas do both would mean a rule for each of its lines on a document, and
drawing a whiteboard to a PNG means nothing.

## Decision

**A `collage` is an element of its own**, beside `canvas`, which doesn't change:

```one
collage poster_page.layers size width height {
	place layer::place
	crop layer::crop
	snap to edges
}
```

- It's as big as `size` says, scaled to fit where it's shown, and drawn at its own
  size in full screen.
- Its layers are drawn in the list's order, each from its parts: `x y w h`,
  `picture`, `crop_x crop_y crop_w crop_h`, `turns`, `mirrored`, `words`, `size`,
  `color`, `weight`, and the background's `background`, `around`, `zoom`,
  `slide_x` and `slide_y`.

**Each part is kept in the field named like it, unless the collage says another.**
An app whose fields have those names says nothing; one that keeps words in
`caption` says `words caption`, and only that. The compiler works out which field
keeps each part and hands the runtimes that list, so the desktop, web and terminal
runtimes read and send parts and never an app's field names. That keeps an app's
data out of uione's code, the way a library names what it needs, like a range's
`begin` and `end`, and lets a type say otherwise. A canvas's place works the same
way, `at x y  size w h`, its fields always handed to the runtimes.
- What it shares with a canvas is shared below the language: placing and showing a
  card at once and back if refused, and pasting files.

**Where its layers go is worked out once for every target.** The geometry, how the
background sits, resizing with its shape kept, snapping, crops and turns, is one
model in C++ for the desktop and terminal and one in TypeScript for the web, both
held to the same cases, so the targets can't drift.

## Consequences

A drawing app is written in the language, the same on the desktop and the web.
`canvas` keeps meaning a board of cards.

The collage reads its fields by convention, so a project that names them otherwise
says so, as a canvas does with `at left top size width height`.
