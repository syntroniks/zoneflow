# SPDX-License-Identifier: Apache-2.0
"""MkDocs hook: put the browser demo into the site at ``demo/``.

The demo is built by ``firmware/zoneflow-fw/sim/web/build.sh`` into
``firmware/zoneflow-fw/sim/web/dist/`` (not committed: it holds a compiled
WebAssembly module). CI builds it before the site. Without a build, for
example when someone runs ``mkdocs serve`` to edit text, a short page takes
its place and says how to build it, so that links to ``demo/`` still work.
"""

import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DIST = ROOT / "firmware" / "zoneflow-fw" / "sim" / "web" / "dist"

PLACEHOLDER = """<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>ZoneFlow demo: not built</title></head>
<body style="font: 16px/1.5 system-ui, sans-serif; max-width: 40em; margin: 3em auto; padding: 0 1em">
<h1>The demo is not in this build of the site</h1>
<p>Build it with <code>firmware/zoneflow-fw/sim/web/build.sh</code>, then build the site again.</p>
<p><a href="../try-it/">About the demo</a></p>
</body>
</html>
"""


def on_post_build(config):
    target = Path(config["site_dir"]) / "demo"
    if target.exists():
        shutil.rmtree(target)
    if (DIST / "index.html").is_file():
        shutil.copytree(DIST, target)
    else:
        target.mkdir(parents=True)
        (target / "index.html").write_text(PLACEHOLDER, encoding="utf-8")
