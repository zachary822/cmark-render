import os
import resource
import signal
import sys
import threading
import time
import unittest
from concurrent.futures import ThreadPoolExecutor

from cmark_render import render_html


class RenderHtmlTests(unittest.TestCase):
    def test_strikethrough(self) -> None:
        self.assertIn("<del>old</del>", render_html("The ~~old~~ road"))

    def test_single_tildes_stay_literal(self) -> None:
        rendered = render_html('"Got~cha~♪"')

        self.assertNotIn("<del>", rendered)
        self.assertIn("Got~cha~♪", rendered)

    def test_passes_raw_html_through(self) -> None:
        self.assertEqual(
            render_html("<em>Roar</em> of the <Dragon King>"),
            "<p><em>Roar</em> of the <Dragon King></p>\n",
        )

    def test_tables(self) -> None:
        rendered = render_html("| Name | Rank |\n|---|:-:|\n| Kaito | S |")

        self.assertIn("<table>", rendered)
        self.assertIn("<th>Name</th>", rendered)
        self.assertIn('<td align="center">S</td>', rendered)

    def test_unicode(self) -> None:
        self.assertEqual(
            render_html("「会話」と**強調** 🐉"),
            "<p>「会話」と<strong>強調</strong> 🐉</p>\n",
        )

    def test_rejects_non_str(self) -> None:
        with self.assertRaises(TypeError):
            render_html(b"bytes")  # type: ignore[arg-type]

    def test_releases_memory(self) -> None:
        # cmarkgfm leaked each render's syntax tree and HTML, ~4x the input.
        text = "\n\n".join(["Some *markdown*, ~~struck~~, | a | table |."] * 1800)
        for _ in range(50):
            render_html(text)
        before = _peak_rss_mb()
        for _ in range(2000):
            render_html(text)

        self.assertLess(_peak_rss_mb() - before, 20)

    def test_renders_correctly_from_many_threads(self) -> None:
        # Each parse sets and clears cmark's global "~" entry; overlapping
        # parses would drop strikethrough.
        text = "\n\n".join(
            f"Line {n}: the ~~old~~ road and ~~new~~ path" for n in range(40)
        )
        expected = render_html(text)

        with ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(lambda _: render_html(text), range(800)))

        self.assertTrue(all(result == expected for result in results))

    def test_renders_in_a_child_forked_mid_render(self) -> None:
        text = "\n\n".join(["Some ~~struck~~ text."] * 2000)
        stop = threading.Event()

        def keep_rendering() -> None:
            while not stop.is_set():
                render_html(text)

        renderer = threading.Thread(target=keep_rendering)
        renderer.start()
        try:
            time.sleep(0.05)
            pid = os.fork()
            if pid == 0:
                # A render lock inherited in the locked state would hang here.
                ok = "<del>struck</del>" in render_html("Some ~~struck~~ text.")
                os._exit(0 if ok else 1)
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
