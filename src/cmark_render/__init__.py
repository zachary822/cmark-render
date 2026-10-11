"""Fast, leak-free markdown to HTML with a vendored cmark-gfm."""

from cmark_render._cmark import render_html

__all__ = ["render_html"]
