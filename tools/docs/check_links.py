#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check internal links and images in the built documentation site.

MkDocs --strict checks Markdown links between pages. It does not check raw
HTML (<img src>, <a href> inside admonitions or tables written as HTML), links
to generated files added by hooks, or anchors inside generated files. This
script reads every HTML file under the site directory and checks that each
relative href and src resolves to a file in the site, and that a #fragment
on a link to an HTML page names an id on that page.

External links (with a scheme or starting with //) are not checked: CI must
not depend on other sites being up.

Usage: python3 tools/docs/check_links.py [site_dir]
Exit status 1 if any link is broken.
"""

import sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit

# Pages whose links are written by JavaScript at run time, or vendored
# third-party files that are not ours to fix.
SKIP_DIRS = ("assets/vendor/", "assets/javascripts/", "assets/stylesheets/", "search/")
# The installer page links into the firmware release that CI copies into the
# site only after the build; it is absent in a pull request build.
OPTIONAL_PREFIXES = ("firmware/releases/latest/",)


class Collector(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links = []  # (attr, value)
        self.ids = set()

    def handle_starttag(self, tag, attrs):
        for name, value in attrs:
            if value is None:
                continue
            if name in ("id", "name"):
                self.ids.add(value)
            if (tag, name) in (("a", "href"), ("img", "src"), ("link", "href"), ("script", "src"),
                               ("source", "src"), ("iframe", "src"), ("object", "data")):
                self.links.append((tag, value))


def parse(path, cache):
    if path not in cache:
        c = Collector()
        c.feed(path.read_text(encoding="utf-8", errors="replace"))
        cache[path] = c
    return cache[path]


def resolve(site, page, target):
    """Return the file a relative URL points to, or None if it does not exist."""
    base = page.parent
    p = (base / unquote(target)).resolve() if not target.startswith("/") else None
    if p is None:
        return None
    if p.is_dir():
        p = p / "index.html"
    return p if p.is_file() else None


def main():
    site = Path(sys.argv[1] if len(sys.argv) > 1 else "site").resolve()
    if not site.is_dir():
        print(f"no site directory: {site}", file=sys.stderr)
        return 2
    cache = {}
    broken = []
    pages = sorted(site.rglob("*.html"))
    checked = 0
    for page in pages:
        rel_page = page.relative_to(site).as_posix()
        if rel_page.startswith(SKIP_DIRS) or rel_page == "404.html":
            continue
        doc = parse(page, cache)
        for tag, url in doc.links:
            parts = urlsplit(url)
            if parts.scheme or url.startswith("//") or url.startswith(("mailto:", "javascript:", "data:")):
                continue
            if not parts.path:
                # Same-page anchor.
                frag = unquote(parts.fragment)
                checked += 1
                if frag and frag not in doc.ids:
                    broken.append((rel_page, url, "no such anchor on this page"))
                continue
            if parts.path.startswith("/"):
                broken.append((rel_page, url, "absolute path; use a relative link"))
                continue
            checked += 1
            target = resolve(site, page, parts.path)
            if target is None:
                joined = (page.parent / unquote(parts.path)).resolve()
                try:
                    rel_target = joined.relative_to(site).as_posix()
                except ValueError:
                    rel_target = ""
                if rel_target.startswith(OPTIONAL_PREFIXES):
                    continue
                broken.append((rel_page, url, "target does not exist"))
                continue
            frag = unquote(parts.fragment)
            if frag and target.suffix == ".html":
                if frag not in parse(target, cache).ids:
                    broken.append((rel_page, url, "no such anchor in the target page"))
    for page, url, why in broken:
        print(f"{page}: {url}: {why}")
    print(f"{checked} internal links checked in {len(pages)} pages, {len(broken)} broken")
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
