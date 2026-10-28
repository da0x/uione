# 0025: A collage's layer may be a shape, drawn by a pinned copy of nanosvg

- Status: Accepted
- Date: 2026-10-28
- Deciders: Daher Alfawares
- Extends: [0019](0019-a-collage-is-a-document.md), [0021](0021-every-picture-in-a-collage-is-a-layer.md);
  in part, [0016](0016-pictures-are-read-by-a-pinned-stb.md), which
  named stb as the only code kept from another project for pictures

## Context

A collage is a picture of a fixed size made of layers (0019), each a picture or
words. A drawing app is the same thing with shapes among them: rectangles, ellipses
and paths, filled and stroked. A poster or a wallpaper wants them too, a banner
behind its words. A second element beside the collage for them would split one
stack of layers in two, and the lesson of 0021 is that every layer is a layer like
the rest.

The web draws on a canvas that fills, strokes and curves with smooth edges. The
desktop's canvas draws rounded boxes, round-ended lines and even-odd fills with no
smoothing and no curves, so it can't draw a shape well.

## Decision

**A layer that's neither a picture nor words is a shape**, as new collage parts say:

- `shape`: a rectangle, an ellipse or a path; a rectangle's corners are rounded as
  far as `corners` says, as a picture's are;
- `points`: a path's, in SVG path notation, `M`, `L`, `C` and `Z`, each point a
  fraction of the layer's box from its top left, as a crop is kept, so moving and
  resizing the layer never changes them;
- `fill` and `stroke`: each `#rrggbb`, or none for anything else;
- `stroke_width`: in the collage's pixels, half of it outside the outline, as SVG
  draws it, never thickened by a resize;
- `opacity`: how much of any layer shows, from 0 to 1.

A shape is picked, moved and snapped as any layer is, and resized freely, its shape
kept only with Shift, the other way from a picture.

Each shape is written as one SVG element by the shared model, `svg_element` in
`one::screens::collage` and `svgElement` in `@uione/react`, held to the same cases.
That's what a drawing saved as SVG holds, and on the desktop it's what's drawn:

- `desktop/src/nanosvg/` holds `nanosvg.h` and `nanosvgrast.h` from
  memononen/nanosvg at commit `239e102`, exactly as fetched, under the zlib license
  beside them. The build checks each file's SHA-256 and stops when either differs.
  `tools/check-license` exempts them.
- `one::desktop::picture::from_svg`, in `desktop/src/svg.cpp`, is the only code that
  includes them. A shape is drawn from its SVG at the size it's shown, and kept until
  its look or size changes.

The web draws the same shape with its canvas's own paths.

## Consequences

A collage has shapes on both targets, drawn alike, and an app such as mural can have
them with no more than the fields.

Text and pictures are drawn as they were; nanosvg draws only shapes.

Moving to a newer nanosvg is a commit that changes the files and their hashes
together. Code from another project stays the exception: each copy named in a
decision, pinned by its hash, and kept as it was.
