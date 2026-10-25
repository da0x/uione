# 0016: Pictures are read and written by a pinned copy of stb

- Status: Accepted
- Date: 2026-10-25
- Deciders: Daher Alfawares
- Supersedes: in part, [0013](0013-everything-is-agpl.md), which named libember as the
  only code kept from another project

## Context

A desktop app shows pictures and saves them, so the toolkit has to read PNG and
JPEG and write PNG. The system's libpng and libjpeg work on Linux, but each is
another library to install, and on Windows one to ship beside the program. An app
built with uione should be one program that runs as it is.

## Decision

`desktop/src/stb/` holds `stb_image.h` and `stb_image_write.h` from
nothings/stb at commit `003501f`, exactly as they were fetched. They're public domain
(or MIT, at the user's choice), compiled into the toolkit, and read PNG, JPEG, GIF,
BMP and TGA, and write PNG.

The build checks each file's SHA-256 and stops when either differs, so a changed copy
is never compiled. `tools/check-license` exempts the two files, which keep their own
terms. Moving to a newer stb is a commit that changes the files and their hashes
together.

`one::desktop::picture` is the only code that includes them, so they can be replaced
without changing anything that draws a picture.

## Consequences

An app shows and saves pictures with nothing installed beside it, on any system.

WebP isn't read. When it's needed, it's a library to add behind `picture`, decided
then.

Code from another project is still the exception: each copy is named in a decision,
pinned by its hash, and kept as it was.
