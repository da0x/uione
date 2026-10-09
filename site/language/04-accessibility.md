# Accessibility

A site built with uione aims at WCAG 2.2 AA, the standard the ADA, Section 508 and
the European Accessibility Act all point to. Some of it the compiler checks, some the
component set draws, and some is the author's, since only they know what their words
and pictures mean. This page says which is which.

Every release is checked with axe-core against WCAG 2.0, 2.1 and 2.2, A and AA, on
neotrac's screens in light, dark and with more contrast. An automated check finds
about a third of what WCAG asks, so the rest is checked by hand, with the keyboard
alone, at 200% text, and in Windows' contrast themes.

## What the compiler checks

| WCAG | What | How |
|---|---|---|
| 1.4.3 Contrast | text reads on what it sits on, 4.5:1 | a theme's colors that don't are a mistake, fixed to the nearest that do |
| 1.4.11 Non-text contrast | the edge of a field, 3:1 | worked out from the theme, never written |
| 1.3.1, 3.3.2, 4.1.2 Labels | every form field has one | an empty label is a mistake |
| 2.4.2 Page titled | every screen has a title | a screen is declared with one |
| 3.1.1 Language of page | the page says its language | written on every page |

## What the component set draws

| WCAG | What |
|---|---|
| 2.1.1 Keyboard | everything that can be clicked can be reached and used with the keyboard |
| 2.4.1 Bypass blocks | Skip to content, the first thing the keyboard reaches |
| 2.4.7, 2.4.11 Focus | a focus ring on whatever has focus, never hidden under the header |
| 1.4.4 Resize text, 1.4.10 Reflow | sizes in rem, so text grows to 200% and the page reflows without scrolling sideways |
| 1.4.12 Text spacing | the page bears WCAG's spacing, which the display settings apply |
| 1.4.1 Use of color | how urgent something is, and what's wrong, is said in words beside its color |
| 2.3.3 Animation | less motion when the reader's system asks |
| 4.1.3 Status messages | what changed, and what went wrong, is announced |

A site with `accessibility menu` also offers its readers display settings beside
light and dark: more contrast, text up to twice as large, comfortable spacing, a
legible font, less motion, solid panels, every link underlined and a thicker focus
ring, each kept in their browser. Every site follows a reader's system when it asks
for more contrast or less motion, and keeps the colors a system picks for itself.

## What's the author's

- **Words.** A link says where it goes, and a button what it does: `"Delete issue"`,
  not `"Click here"`.
- **Pictures.** An icon or a picture that says something has words for it; one that
  only decorates doesn't.
- **Video and sound** have captions and transcripts.
- **Hand-written components**, in `components/`, are held to the same standard: a
  label for each control, the keyboard for everything, and the theme's colors.
