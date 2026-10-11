# cmark-render

Markdown to HTML in C, using a vendored, statically compiled
[cmark-gfm](https://github.com/github/cmark-gfm).

```python
from cmark_render import render_html

render_html("| a | b |\n|---|---|\n| ~~old~~ | **new** |")
```

`render_html` renders CommonMark plus:

- GFM tables
- strikethrough, on `~~double~~` tildes only (single `~` is left alone)
- raw HTML passed through unchanged (`CMARK_OPT_UNSAFE`). **Sanitize the
  output** if the input is untrusted.

There are no options. The one function is all the package provides.

## Why not cmarkgfm?

`cmarkgfm` 2025.10.22 never frees the syntax tree or the rendered HTML, so
each call leaks about 4x the size of its input. This binding frees every
allocation that cmark hands back.

It is also safe to use with threads and `fork()`. Renders run without the GIL
but one at a time, because cmark-gfm keeps a global table of special
characters that changes during each parse. A `pthread_atfork` handler stops a
child process from inheriting a held lock.

## Install

```sh
pip install cmark-render
```

Wheels are built for CPython 3.14 on macOS arm64 and manylinux x86_64 and
aarch64. On other platforms, pip builds from the sdist, which needs a C
compiler.

## License

BSD-2-Clause. The vendored cmark-gfm is under its own licenses, in
`vendor/cmark-gfm/COPYING`. `vendor/cmark-gfm/UPSTREAM` records the upstream
commit.
