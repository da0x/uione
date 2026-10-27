# 0022: A paper is a page written on in place

- Status: Accepted
- Date: 2026-10-27
- Deciders: Daher Alfawares

## Context

A document app shows its document as a page and is typed on where the words are, as
a word processor is. A form with a box for the whole text, and the text shown set
above it, is a different thing: what's written isn't what's read, and there's
nowhere to say that one part is a title and another a quote.

A collage is a picture made of layers placed anywhere. A page is words flowing down
a sheet, one block after another, each a kind of block, aligned on its own. Neither
is a canvas of cards.

## Decision

**A `paper` is an element of its own**, beside `collage`:

```one
paper document_page.blocks {
	write block::write
	add block::create
	delete block::delete
	empty "Title"
}
```

- Its rows are its blocks, in the order their `position` says. Each is set as its
  `kind` says, `title`, `subtitle`, `heading`, `text`, `quote`, `math` or `code`, and aligned
  as its `align` says. That and the page's face are all of its style; there are no
  fonts, sizes or colors to pick.
- It's typed on in place, a block at a time. Enter splits a block, its words after
  the caret made one of their own by `add`; Backspace at a block's start joins it to
  the one before, deleting it by `delete`. Each is one step undo takes back, and
  typing is sent once it pauses, or when the caret leaves the block.
- The block the caret is in is the screen's pick of its list, so a toolbar sets the
  block being written with ordinary command buttons and given values.
- Each part is kept in the field named like it unless the paper says another, as a
  collage's are, and the compiler hands the runtimes which, so they read and send
  parts and never an app's fields.

**A page has a face**, the type it's all set in, chosen for the kind of writing it
is rather than as a font: `book`, `modern`, `lyric` or `typewriter`. It's a value of
the paper's view, kept on what the view is per, like a document. uione brings book's
Literata and lyric's Fraunces with it on both targets; modern and typewriter are the
app's own text and code faces.

**It reads as it's written.** A heading is ruled under across the whole line, and
`outline` lists the title and headings beside the page, the section the caret is in
marked, each going there. Its place is kept whether there are headings or not, so the
page doesn't move as they come and go.

**A math block is its equation, set**, from the notation its words are written in,
until the caret is in it, when it's its words. It keeps the room of the larger, so
the page doesn't move as the caret comes and goes.

**A toolbar inserts a block with `insert`**, a button giving the values of a block
made after the caret's, like `insert "Equation" icon equation { kind = kind::math }`.

**A toolbar's buttons leave the keyboard where it was**, so the caret stays in the
block they just changed. That's true of every toolbar, on both targets.

**The desktop and the web keep the same rules**: the sheet's size and margins, each
kind's look and the room around it, what Enter and Backspace do, and the shortcuts
`# ` and `> `. The desktop draws it as one element holding the caret; the web types
into each block where it is.

## Consequences

A document app is written in the language, the same on the desktop and the web. A
terminal isn't written on in place, so it has no paper yet.

Positions are numbers, and a block made between two goes halfway between them, so
nothing else is renumbered as blocks are split and joined.
