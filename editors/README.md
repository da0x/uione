# editors

Highlighting for `.one` files. The Vim and VS Code grammars are written separately
and kept identical by a test.

## Vim and Neovim

`editors/vim` is a normal runtime folder with `ftdetect`, `syntax` and `ftplugin`.
Load it any one of these ways:

```sh
# native package (Vim 8+, Neovim)
mkdir -p ~/.vim/pack/uione/start
ln -s ~/uione/editors/vim ~/.vim/pack/uione/start/uione
```

```vim
" vim-plug
Plug 'da0x/uione', { 'rtp': 'editors/vim' }

" or directly
set runtimepath+=~/uione/editors/vim
```

For Neovim, use `~/.local/share/nvim/site/pack/uione/start/uione` instead of the
`~/.vim` path above.

The ftplugin sets `//` as the comment string (so `gc` and `:Comment` work), two-space
indentation, and indentation after `{`.

## VS Code

```sh
editors/vscode/build                                   # writes uione-<version>.vsix
code --install-extension editors/vscode/uione-0.0.1.vsix
```

`build` needs only Python 3. For working on the grammar, link the folder in instead,
and reload the window after each edit:

```sh
ln -s ~/uione/editors/vscode ~/.vscode/extensions/uione
```

The same folder also works in VSCodium, Cursor, and JetBrains IDEs (Settings,
Editor, TextMate Bundles, add `editors/vscode`).

## Changing the language

The language and its highlighting move together. When a `.one` file gains or loses
a word:

1. Update `vim/syntax/uione.vim` and `vscode/syntaxes/uione.tmlanguage.json`.
2. Add a line for the new word to `expected.tsv`, naming the file it appears in.
3. Run `node editors/test.mjs`.

The test opens each file named in `expected.tsv` in real, headless Vim. It runs
the VS Code grammar through a small TextMate tokenizer. Every line of `expected.tsv`
has to come out the same in both.
