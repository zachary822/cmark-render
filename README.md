# cmark-render

Markdown to HTML in C, using a vendored, statically compiled
[cmark-gfm](https://github.com/github/cmark-gfm).

```python
from cmark_render import Extension, Option, render_html

render_html(
    "| a | b |\n|---|---|\n| ~~old~~ | **new** |",
    extensions=[Extension.TABLE, Extension.STRIKETHROUGH],
)
```

With no options or extensions, `render_html` is plain cmark-gfm: CommonMark
with its default options, so raw HTML and `javascript:`-style links are
dropped.

## Extensions

`extensions` takes the names of cmark-gfm's core syntax extensions, as
`Extension` members or plain strings. None are attached by default.

| `Extension` | Effect |
| --- | --- |
| `TABLE` | GFM pipe tables. |
| `STRIKETHROUGH` | `~~text~~` and `~text~` become `<del>`. |
| `AUTOLINK` | Bare `www.` and `https://` URLs become links. |
| `TASKLIST` | `- [ ]` and `- [x]` list items become checkboxes. |
| `TAGFILTER` | Escapes `<script>`, `<iframe>`, `<style>` and other risky tags even under `UNSAFE`. |

An unknown name raises `ValueError`, and repeating a name has no extra effect.

## Options

`options` takes cmark-gfm's `CMARK_OPT_*` flags, as `Option` members combined
with `|`:

```python
render_html(text, options=Option.SMART | Option.FOOTNOTES)
render_html(text, options=Option.UNSAFE)  # keep raw HTML; sanitize untrusted input
```

| `Option` | Effect |
| --- | --- |
| `UNSAFE` | Pass raw HTML and `javascript:`-style links through. **Sanitize the output** if the input is untrusted. |
| `STRIKETHROUGH_DOUBLE_TILDE` | With `STRIKETHROUGH`, strike through only on `~~`, leaving single `~` alone. |
| `SMART` | Curly quotes, en and em dashes, ellipses. |
| `HARDBREAKS` | Render soft line breaks as `<br />`. |
| `NOBREAKS` | Render soft line breaks as spaces. |
| `SOURCEPOS` | Add `data-sourcepos` attributes to block elements. |
| `FOOTNOTES` | Parse `[^1]` footnotes. |
| `VALIDATE_UTF8` | Accepted, but has no effect: a `str` always arrives as valid UTF-8. |
| `GITHUB_PRE_LANG` | Write `<pre lang="x">` for fenced code instead of a `class` on `<code>`. |
| `FULL_INFO_STRING` | Keep the rest of a code fence's info string, in `data-meta`. |
| `LIBERAL_HTML_TAG` | Be looser about what counts as an inline HTML tag. |
| `TABLE_PREFER_STYLE_ATTRIBUTES` | With `TABLE`, align cells with `style` instead of `align`. |

An unknown bit raises `ValueError`.

## Memory and threads

Every render frees its syntax tree and rendered HTML, so memory stays flat
however many documents you render.

It is safe to use with threads and `fork()`. Renders run without the GIL.
Parsing runs one at a time, because cmark-gfm keeps a global table of special
characters that changes during each parse, but writing the HTML runs in
parallel. A `pthread_atfork` handler stops a
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
