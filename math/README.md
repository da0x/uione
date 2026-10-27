# one::math

A notation for equations that reads the way you'd type one in a message, and is set
the way a book would set it.

```
x = (-b +- sqrt(b^2 - 4a c)) / 2a
sum(k = 1 to n) k^2 = n(n + 1)(2n + 1) / 6
int(0 to inf) e^(-x^2) dx = sqrt(pi) / 2
f(x) = cases(x if x >= 0; -x otherwise)
```

There are no backslashes and no braces to balance, and nothing to learn before writing
the first equation. Anyone who has typed `x^2` or `a/b` already writes it. The same
text is what a program works out, so a formula and the equation it's shown as can't
disagree.

It's a library with one grammar and two implementations, C++ and TypeScript, held to
one shared file of cases, so a window and a web page set every equation alike.

## Writing it

### Numbers and names

- Numbers are written as usual: `2`, `0.5`, `6.02e23`. A number followed by `%` is a
  percent: `6.5%` is 0.065.
- A name of one letter is set in italic: `x`, `n`.
- A Greek letter is written by its name and set as the letter: `alpha`, `zeta`, `pi`,
  `Omega` (capital first for the capital).
- A longer name is set upright, as a word: `rate`, `loan`. Underscores in it are set
  as spaces: `monthly_rate` is "monthly rate".
- Letters together are one name, so a times c is `a c`, with a space: `4a c`, not
  `4ac`. That's what lets `rate` be a name and not r times a times t times e.
- `inf` is ∞.

### Putting things together

| Write | Set as |
|---|---|
| `a + b`, `a - b`, `a +- b` | a + b, a − b, a ± b |
| `a * b` | a b, or 2 × 3 between two numbers |
| `2a`, `2 pi r`, `n(n + 1)` | side by side, multiplied |
| `a / b` | a stacked over b |
| `x^2`, `e^(i pi)` | a power, raised |
| `x_1`, `x_max`, `x_(i + 1)` | a subscript, lowered |
| `f'(x)`, `n!` | a prime, a factorial |
| `|x|` | the absolute value |
| `"for every n"` | words, upright, inside an equation |

Parentheses that only group what a fraction, power, subscript or root holds aren't
drawn: `(a + b) / 2` is a + b stacked over 2, with no parentheses.

**`_` makes a subscript after one letter or a Greek name** (`x_1`, `theta_0`,
`x_max`). After a longer name it's part of the name (`monthly_rate`). So names can be
the snake_case names a program already uses.

### What binds tighter

From loosest to tightest:

1. Relations: `=`, `!=`, `<`, `>`, `<=`, `>=`, `~=`, `->`, `=>`, `<=>`, `in`. They can
   be chained: `0 < x <= 1`.
2. `+`, `-`, `+-`.
3. `*` and `/`.
4. Side by side: `2a`, `2 pi r`, `n(n + 1)`.
5. A minus in front: `-x`.
6. `^`, `_`, `'`, `!`, `%`. A power is read right to left: `2^3^2` is 2^(3^2).

**Side by side binds tighter than `/`**, as it does in a textbook: `1 / 2a` is one
over 2a, and `n(n + 1) / 2` is all of n(n + 1) over 2. Write `(1 / 2) a` to mean the
other.

### Functions

- `sqrt(x)` is √x, and `root(3, x)` is its cube root.
- `sin`, `cos`, `tan`, `ln`, `log`, `exp`, `min`, `max` and the like are set upright.
  They take parentheses, or the side-by-side term after them: `sin 2x` is sin(2x), and
  `sin x / 2` is half of sin x.
- `floor(x)`, `ceil(x)` and `abs(x)` are set with their brackets: ⌊x⌋, ⌈x⌉, |x|.
- Any other name followed by parentheses is set as written: `f(x)`, `g(t + 1)`.

### Big operators

The bounds go in parentheses, with `to` between them; what they act on follows.

| Write | Set as |
|---|---|
| `sum(k = 1 to n) k^2` | Σ with k = 1 under it and n over it |
| `prod(i = 1 to n) x_i` | Π the same way |
| `int(0 to 1) x^2 dx` | ∫ from 0 to 1 |
| `int x dx` | ∫ with no bounds |
| `lim(x -> 0) sin x / x` | lim with x → 0 under it |

What one acts on is the side-by-side term after it, so `sum(k = 1 to n) k + 1` is
the sum, then one more.

### Calculus

- `dx`, `dy`, `dt`: a `d` and one letter is set as an upright d and the letter.
- `dy / dx` and `d / dx f(x)` are derivatives as fractions, as written.
- `partial` is ∂: `partial f / partial x`.

### Accents

`hat(x)`, `bar(x)`, `vec(v)`, `dot(x)` and `ddot(x)` put their mark over what they
hold.

### Matrices and cases

- `[1, 2; 3, 4]` is a matrix: commas between columns, semicolons between rows.
  `[1, 2, 3]` is one row.
- `cases(x if x >= 0; -x otherwise)` is a brace with each case on a line, its
  condition beside it.

### Symbols typed directly

Every symbol above can also be typed as itself: `π`, `≤`, `±`, `√`, `∑`, `∞`, `→`.
`a ≤ b` and `a <= b` are the same equation.

## Working it out

The part of the notation a program can compute is worked out the same way on every
target:

- numbers, `%`, names given values, `+ - * /`, side by side, `^`;
- `sqrt`, `root`, `exp`, `ln`, `log`, `sin`, `cos`, `tan`, `abs`, `floor`, `ceil`,
  `round`, `min`, `max`;
- `sum` and `prod` with number bounds;
- relations, giving true or false;
- `cases`, the first case whose condition holds.

Integrals, limits, derivatives, matrices and words are set but not worked out. A
formula holding one says so: "an integral isn't worked out".

## Setting it

An equation is laid out as boxes, each with a width, a height above its baseline and a
depth below it. Fractions, powers, roots, operators with limits, matrices and cases
are placed by the rules of the font's OpenType MATH table.

Both implementations read the font's measures from one table made once from the font
file: each glyph's advance and box, and the MATH table's constants. Neither asks a
browser or a font library how wide anything is, so both place every glyph in the same
spot.

A terminal sets an equation in Unicode: `x²`, `x₁`, `√x`, `∑`. Where there's room it
stacks a fraction over a line of `─`, and it writes `a/b` when there isn't.

## In Markdown

`$…$` holds an equation in a line of text, and a ```` ```math ```` block holds one set
on its own lines, centered.

## Status

Designed. Nothing is built yet.
