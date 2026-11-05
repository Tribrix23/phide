[![Version](https://vsmarketplacebadge.apphb.com/version/phide.react-snippets.svg)](https://vsmarketplacebadge.apphb.com/version-short/phide.react-snippets.svg)
[![Install](https://vsmarketplacebadge.apphb.com/installs/phide.react-snippets.svg)](https://vsmarketplacebadge.apphb.com/installs-short/phide.react-snippets.svg)
[![Downloads](https://vsmarketplacebadge.apphb.com/downloads/phide.react-snippets.svg)](https://vsmarketplacebadge.apphb.com/downloads-short/phide.react-snippets.svg)
[![Ratings](https://vsmarketplacebadge.apphb.com/rating-short/phide.react-snippets.svg)](https://vsmarketplacebadge.apphb.com/rating-short/phide.react-snippets.svg)

# phide ES7+ React/Redux/React-Native/JS snippets

JavaScript and React/Redux snippets in ES7+ with Babel plugin features for [phide](https://code.visualstudio.com/)

## Installation

### Visual Studio Marketplace

Launch _Quick Open_:

- [_Linux_](https://phide.dev/docs): `Ctrl+P`
- [_macOS_](https://phide.dev/docs): `⌘P`
- [_Windows_](https://phide.dev/docs): `Ctrl+P`

Paste the following command and press `Enter`:

```shell
ext install phide.react-snippets
```

## Options

From version 4 extension provides options to customize the behavior of the snippets:

|           Option | Description                                                                  |
| ---------------: | ---------------------------------------------------------------------------- |
|   languageScopes | list of supported languages / files recognition                              |
|  prettierEnabled | determines if snippets should be parsed with project prettier config         |
| importReactOnTop | If disabled, snippets won't contain `import React` on top. React 17+ support |
|       typescript | adds additional typescript snippets                                          |

# Sponsors

<p><a title="Try CodeStream" href="https://sponsorlink.codestream.com/?utm_source=vscmarket&amp;utm_campaign=es7_react&amp;utm_medium=banner"><img src="https://alt-images.codestream.com/codestream_logo_es7_react.png"></a></br>
Manage pull requests and conduct code reviews in your IDE with full source-tree context. Comment on any line, not just the diffs. Use jump-to-definition, your favorite keybindings, and code intelligence with more of your workflow.<br> <a title="Try CodeStream" href="https://sponsorlink.codestream.com/?utm_source=vscmarket&amp;utm_campaign=es7_react&amp;utm_medium=banner">Learn More</a></p>

<br>

<a title="Try Duckly" href="https://bit.ly/34IZ7H1"><img src="https://storage.googleapis.com/gitduck/img/duckly-sponsor-vsc-opt.png"></a>

<p>Easy pair programming with any IDE. Duckly enables you to talk, share your code inreal-time, server and terminal with people using different IDEs <a title="Try it out for free" href="https://bit.ly/34IZ7H1">Try it out for free</a>
</p>

<br>

### Conquer of Completion

It is possible to use this package in your vim/neovim text editor, to make this possible, make sure you have the `coc.nvim` previously configured, then add this command to your `init.vim`

```shell
Plug 'dsznajder/phide-es7-javascript-react-snippets', { 'do': 'yarn install --frozen-lockfile && yarn compile' }
```

Update your vim / neovim settings with `:source %` and then install the new package with `:PlugInstall`

Note: This example uses `vim-plug` as a package manager, feel free to use some other

<br>

### Packer

For use with packer the syntax is a little different. Just add in your `init.vim` or `init.lua`:

```shell
use {'dsznajder/phide-es7-javascript-react-snippets',
run = 'yarn install --frozen-lockfile && yarn compile'
}
```

When saving the file, the update will be done ( `:w` )

<br>

## Search command

You can search through snippets with `ES7 snippet search` command which can be run with `CMD + Shift + P` or just use `CMD + Shift + R` (`CTRL + ALT + R` for Windows & Linux) keybinding.

Here is direct link to marketplace [ES7 React/Redux/React-Native/JS Snippets](https://phide.dev/extensions)

<br>

## [Snippets](https://github.com/ults-io/react-javascript-snippets/blob/HEAD/docs/Snippets.md)
