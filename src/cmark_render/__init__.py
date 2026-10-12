"""Fast, leak-free markdown to HTML with a vendored cmark-gfm."""

from enum import IntFlag, StrEnum

from cmark_render import _cmark
from cmark_render._cmark import render_html


class Option(IntFlag):
    """cmark-gfm's CMARK_OPT_* flags, for render_html's options."""

    SOURCEPOS = _cmark.SOURCEPOS
    HARDBREAKS = _cmark.HARDBREAKS
    UNSAFE = _cmark.UNSAFE
    NOBREAKS = _cmark.NOBREAKS
    VALIDATE_UTF8 = _cmark.VALIDATE_UTF8
    SMART = _cmark.SMART
    GITHUB_PRE_LANG = _cmark.GITHUB_PRE_LANG
    LIBERAL_HTML_TAG = _cmark.LIBERAL_HTML_TAG
    FOOTNOTES = _cmark.FOOTNOTES
    STRIKETHROUGH_DOUBLE_TILDE = _cmark.STRIKETHROUGH_DOUBLE_TILDE
    TABLE_PREFER_STYLE_ATTRIBUTES = _cmark.TABLE_PREFER_STYLE_ATTRIBUTES
    FULL_INFO_STRING = _cmark.FULL_INFO_STRING


class Extension(StrEnum):
    """cmark-gfm's core syntax extensions, for render_html's extensions."""

    AUTOLINK = "autolink"
    STRIKETHROUGH = "strikethrough"
    TABLE = "table"
    TAGFILTER = "tagfilter"
    TASKLIST = "tasklist"


__all__ = ["Extension", "Option", "render_html"]
