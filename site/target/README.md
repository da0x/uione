# target

What `one build site` must produce, kept as reviewed files so the generators can be
tested against them. Each file carries this repository's license header; the
generated copy doesn't, because generated code belongs to whoever generated it, and
the tests strip the header before comparing.

## web

`web/` is exactly what the web generator writes, and a golden test in
`compiler/tests/web_tests.cpp` fails if the two ever differ. Changing the generator's
output means changing these files in the same commit, which is the point: the
change to what gets generated is reviewed as a diff of the generated code.

| `.one` | React (`web/`) |
|---|---|
| `home.one` | `src/screens/home.tsx` |
| `language.one` | `src/screens/language.tsx` |
| `releases.one` | `src/screens/releases.tsx` |
| `mission.one` | `src/screens/mission.tsx` |
| `site.one`, its footer | `src/footer.tsx` |
| `install.one` | `src/screens/install.tsx` |
| every file | `src/app.tsx`, `src/main.tsx`, `package.json`, `tsconfig.json`, `vite.config.ts` |

`src/pages/*.generated.ts` and `index.html` are generated too but aren't kept here. They
change whenever a page does, so they're tested for their shape instead.

## api and firestore.rules

`api/` and `firestore.rules` are exactly what the backend and rules generators write,
held by golden tests in `compiler/tests/api_tests.cpp`, the same way as `web/`.

`api/library/library.go` is the backend of `examples/library/main.one`, the example
that uses every part of the language, held to its target the same way. It shows the
harder shapes: commands that change the book a loan points at, rows read through a
reference, a value worked out from another entity, and a function.

## cpp

`cpp/uione.hpp` is exactly what the C++ client generator writes for the site, held by
a golden test in `compiler/tests/cpp_tests.cpp`. It's one header a native program
includes: a struct for every entity and view, the ids entities are stored under, a
function for every command, and every view read live through libember.

The rule all of these are held to: a generated file is about as long as the `.one`
it came from. Anything that would repeat across generated files belongs in a library
instead.
