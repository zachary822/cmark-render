from pathlib import Path

from setuptools import Extension, setup

CMARK = Path("vendor/cmark-gfm")
sources = [
    "src/cmark_render/_cmark.c",
    *sorted(str(path) for path in (CMARK / "src").glob("*.c")),
    *sorted(str(path) for path in (CMARK / "extensions").glob("*.c")),
]

setup(
    ext_modules=[
        Extension(
            "cmark_render._cmark",
            sources=sources,
            include_dirs=[str(CMARK / d) for d in ("src", "extensions", "generated")],
            # Compiled in, not linked against a shared libcmark-gfm.
            define_macros=[("CMARK_GFM_STATIC_DEFINE", None)],
        )
    ],
)
