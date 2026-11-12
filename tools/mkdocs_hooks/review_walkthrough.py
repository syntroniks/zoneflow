# SPDX-License-Identifier: Apache-2.0
"""MkDocs hook: the pre-order review walkthrough and its bench sheet.

The content is ``docs/hardware/review/data/*.toml``. The pictures are drawn from
the board file by ``make -C hardware/zoneflow-a review`` into
``hardware/zoneflow-a/output/review/`` and committed. This hook

- publishes the pictures at ``hardware/review/img/``;
- replaces ``<!-- zf:review-walkthrough -->`` and ``<!-- zf:review-bench-sheet -->``
  by the page content;
- fails the build when the content names a part or a net that is not on the
  board, or when the pictures were drawn from another board than the committed
  one. The build has no KiCad; ``reviewwalk.check`` reads the board file as text.
"""

import subprocess
import sys
from pathlib import Path

from mkdocs.exceptions import PluginError
from mkdocs.structure.files import File

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "reviewwalk"))

from reviewwalk import check, spec  # noqa: E402
from reviewwalk import page as pagemod  # noqa: E402

MARKERS = {
    "<!-- zf:review-walkthrough -->": "walkthrough",
    "<!-- zf:review-bench-sheet -->": "bench_sheet",
}
PIN1 = spec.BOARD_DIR / "output" / "zoneflow-a-pin1.csv"
_state = {}


def _content():
    if "spec" not in _state:
        try:
            content = spec.load()
        except spec.SpecError as e:
            raise PluginError(f"review walkthrough: {e}") from e
        problems = check.problems(content)
        if problems:
            raise PluginError("review walkthrough:\n  " + "\n  ".join(problems))
        _state["spec"] = content
        _state["manifest"] = pagemod.load_manifest()
    return _state["spec"], _state["manifest"]


def _commit():
    """The commit this site is built from, for the hand-off file: an agent has to know what was reviewed."""
    def git(*args):
        return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, timeout=20)

    try:
        head = git("rev-parse", "HEAD")
        if head.returncode != 0:
            return "unknown"
        dirty = git("status", "--porcelain", "--untracked-files=no").stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return "unknown"
    return head.stdout.strip() + (" with local changes" if dirty else "")


def on_pre_build(config):
    _state.clear()


def on_files(files, config):
    for src in sorted(spec.OUT.glob("*.svg")):
        files.append(File.generated(config, f"hardware/review/img/{src.name}", abs_src_path=str(src)))
    return files


def on_page_markdown(markdown, page, config, files):
    for marker, what in MARKERS.items():
        if marker not in markdown:
            continue
        content, manifest = _content()
        root = "../" * page.url.rstrip("/").count("/") + ("../" if page.url else "")
        if "commit" not in _state:
            _state["commit"] = _commit()
        builder = pagemod.Builder(content, manifest, root, commit=_state["commit"])
        html = builder.walkthrough() if what == "walkthrough" else builder.bench_sheet(PIN1)
        # One block of raw HTML: no blank lines, so that Markdown leaves it alone.
        html = "\n".join(line for line in html.splitlines() if line.strip())
        markdown = markdown.replace(marker, html)
    return markdown
