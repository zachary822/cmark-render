from collections.abc import Iterable

SOURCEPOS: int
HARDBREAKS: int
UNSAFE: int
NOBREAKS: int
VALIDATE_UTF8: int
SMART: int
GITHUB_PRE_LANG: int
LIBERAL_HTML_TAG: int
FOOTNOTES: int
STRIKETHROUGH_DOUBLE_TILDE: int
TABLE_PREFER_STYLE_ATTRIBUTES: int
FULL_INFO_STRING: int

def render_html(
    text: str, /, options: int = ..., extensions: Iterable[str] = ...
) -> str: ...
