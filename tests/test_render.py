import os
import resource
import signal
import sys
import threading
import time
import unittest
from concurrent.futures import ThreadPoolExecutor

from cmark_render import Extension, Option, render_html

STRIKETHROUGH = [Extension.STRIKETHROUGH]


class RenderHtmlTests(unittest.TestCase):
    def test_defaults_are_plain_commonmark(self) -> None:
        self.assertEqual(
            render_html("<b>x</b> ~~y~~ www.example.com"),
            "<p><!-- raw HTML omitted -->x<!-- raw HTML omitted --> ~~y~~ "
            "www.example.com</p>\n",
        )

    def test_unicode(self) -> None:
        self.assertEqual(
            render_html("「会話」と**強調** 🐉"),
            "<p>「会話」と<strong>強調</strong> 🐉</p>\n",
        )

    def test_strikethrough(self) -> None:
        rendered = render_html("The ~~old~~ and ~new~ road", extensions=STRIKETHROUGH)

        self.assertIn("<del>old</del>", rendered)
        self.assertIn("<del>new</del>", rendered)

    def test_double_tilde_option_leaves_single_tildes(self) -> None:
        rendered = render_html(
            '"Got~cha~♪" ~~gone~~',
            Option.STRIKETHROUGH_DOUBLE_TILDE,
            STRIKETHROUGH,
        )

        self.assertIn("Got~cha~♪", rendered)
        self.assertIn("<del>gone</del>", rendered)

    def test_tables(self) -> None:
        rendered = render_html(
            "| Name | Rank |\n|---|:-:|\n| Kaito | S |", extensions=["table"]
        )

        self.assertIn("<table>", rendered)
        self.assertIn("<th>Name</th>", rendered)
        self.assertIn('<td align="center">S</td>', rendered)

    def test_autolink(self) -> None:
        self.assertIn(
            '<a href="http://www.example.com">www.example.com</a>',
            render_html("www.example.com", extensions=[Extension.AUTOLINK]),
        )

    def test_tasklist(self) -> None:
        rendered = render_html("- [x] done", extensions=[Extension.TASKLIST])

        self.assertIn('<input type="checkbox" checked="" disabled="" />', rendered)

    def test_tagfilter_escapes_script_under_unsafe(self) -> None:
        rendered = render_html(
            "<script>x</script> <b>ok</b>",
            Option.UNSAFE,
            [Extension.TAGFILTER],
        )

        self.assertIn("&lt;script>", rendered)
        self.assertIn("<b>ok</b>", rendered)

    def test_extensions_accept_any_iterable_and_repeats(self) -> None:
        text = "~~x~~ | a |\n|---|\n| b |"
        once = render_html(text, extensions=("strikethrough", "table"))

        self.assertEqual(
            render_html(text, extensions=iter(["table", "strikethrough", "table"])),
            once,
        )

    def test_rejects_unknown_extension(self) -> None:
        with self.assertRaises(ValueError):
            render_html("x", extensions=["emoji"])

    def test_rejects_a_bare_str_for_extensions(self) -> None:
        with self.assertRaises(TypeError):
            render_html("x", extensions="table")

    def test_passes_raw_html_through(self) -> None:
        self.assertEqual(
            render_html("<em>Roar</em> of the <Dragon King>", Option.UNSAFE),
            "<p><em>Roar</em> of the <Dragon King></p>\n",
        )

    def test_smart_punctuation(self) -> None:
        self.assertEqual(
            render_html('"Hi" -- there...', options=Option.SMART),
            "<p>\u201cHi\u201d \u2013 there\u2026</p>\n",
        )

    def test_hardbreaks(self) -> None:
        self.assertEqual(
            render_html("a\nb", options=Option.HARDBREAKS), "<p>a<br />\nb</p>\n"
        )

    def test_sourcepos(self) -> None:
        self.assertEqual(
            render_html("hi", options=Option.SOURCEPOS),
            '<p data-sourcepos="1:1-1:2">hi</p>\n',
        )

    def test_footnotes(self) -> None:
        rendered = render_html("Note[^1]\n\n[^1]: Text", options=Option.FOOTNOTES)

        self.assertIn('<section class="footnotes"', rendered)

    def test_rejects_unknown_options(self) -> None:
        with self.assertRaises(ValueError):
            render_html("x", options=1 << 3)

    def test_rejects_non_str(self) -> None:
        with self.assertRaises(TypeError):
            render_html(b"bytes")  # type: ignore[arg-type]

    def test_releases_memory(self) -> None:
        text = "\n\n".join(["Some *markdown*, ~~struck~~, | a | table |."] * 1800)
        extensions = ["strikethrough", "table"]
        for _ in range(50):
            render_html(text, extensions=extensions)
        before = _peak_rss_mb()
        for _ in range(2000):
            render_html(text, extensions=extensions)

        self.assertLess(_peak_rss_mb() - before, 20)

    def test_renders_correctly_from_many_threads(self) -> None:
        # Each parse sets and clears cmark's global "~" entry; overlapping
        # parses would drop strikethrough.
        text = "\n\n".join(
            f"Line {n}: the ~~old~~ road and ~~new~~ path" for n in range(40)
        )
        expected = render_html(text, extensions=STRIKETHROUGH)
        self.assertEqual(expected.count("<del>"), 80)

        with ThreadPoolExecutor(max_workers=8) as pool:
            results = list(
                pool.map(
                    lambda _: render_html(text, extensions=STRIKETHROUGH), range(800)
                )
            )

        self.assertTrue(all(result == expected for result in results))

    def test_renders_in_a_child_forked_mid_render(self) -> None:
        text = "\n\n".join(["Some ~~struck~~ text."] * 2000)
        stop = threading.Event()

        def keep_rendering() -> None:
            while not stop.is_set():
                render_html(text, extensions=STRIKETHROUGH)

        renderer = threading.Thread(target=keep_rendering)
        renderer.start()
        try:
            time.sleep(0.05)
            pid = os.fork()
            if pid == 0:
                # A render lock inherited in the locked state would hang here.
                rendered = render_html(
                    "Some ~~struck~~ text.", extensions=STRIKETHROUGH
                )
                os._exit(0 if "<del>struck</del>" in rendered else 1)
        finally:
            stop.set()
            renderer.join()

        deadline = time.monotonic() + 10
        while (status := os.waitpid(pid, os.WNOHANG))[0] == 0:
            if time.monotonic() > deadline:
                os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)
                self.fail("child process deadlocked rendering markdown")
            time.sleep(0.01)
        self.assertEqual(os.waitstatus_to_exitcode(status[1]), 0)


def _peak_rss_mb() -> float:
    peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    # ru_maxrss is bytes on macOS and kilobytes on Linux.
    return peak / (1024 * 1024 if sys.platform == "darwin" else 1024)


if __name__ == "__main__":
    unittest.main()
