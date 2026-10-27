# 0021: Every picture in a collage is a layer like the rest

- Status: Accepted
- Date: 2026-10-27
- Deciders: Daher Alfawares
- Supersedes: in part, [0019](0019-a-collage-is-a-document.md), its background

## Context

A collage's first picture was its background. It sat as the view said, fit, fill,
stretch or center, zoomed and slid by the view's own values, and only it filled
the space around it, from its edges, its edge color or a blend of every picture.
Every other picture was placed where its layer said.

So the same picture behaved two ways depending on where it was in the stack. A
picture couldn't be centered with its edges drawn out unless it was the lowest, and
the lowest couldn't be moved or resized like the rest. Melting one picture into
the ones under it had nowhere to go: it isn't about the background, and it isn't
about the whole collage.

## Decision

**There is no background. Every picture is a layer, placed where its `x y w h`
say.** Fit, fill, stretch and center are where a picture is put, once, by a command
that works out its box, not a way it keeps sitting: placed, it moves and resizes
like any layer, and a picture filling the collage is dragged to show another part
of it. The first picture to come into an empty collage fills it.

**What's around a picture is a part of its own layer, `around`:**

- `clear`, the default: nothing.
- `edges` or `color`: the rest of the collage drawn out from its edges, or in its
  edge color, over the layers under it.
- `blend`: the rest of the collage filled from its edges by a harmonic fill.
- `harmonize`: it melts into what's under it, its own colors spread a little past
  its edges and fading, its edges softened under them. It reaches only so far, so
  what's under it shows beyond that.

**Its corners are a part of it too, `corners`**: square at 0, or trimmed round as far
as that share of its shorter side, with no line drawn along them.

The view's `background`, `around`, `zoom`, `slide_x` and `slide_y` parts, and the
collage's `slide` line, are gone.

**`min` and `max` come into command bodies**, so a command can work out where a
picture fits or fills from the collage's size and the picture's own.

## Consequences

Every picture behaves the same wherever it is in the stack, and the collage's
runtimes lose the background's special cases: its placement, its sliding, its
crop, and the picking of it when nothing else is under the pointer.

The halo and the blend are worked out from the picture's own pixels, averaged on
the processor, so they're the same on any thread and in a picture saved or copied.

An app's data from before needs moving: its lowest picture's box worked out from
how it sat, and the view's `around` moved onto it. That's the app's to do, once.
