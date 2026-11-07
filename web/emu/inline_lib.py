#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Put the update protocol's modules (web/fm1pkg.js, web/fm1ota.js: Felucca's) into the page where it
says /*FM1LIB*/, and the content manager's (web/hp1content.js) where it says /*HP1CONTENT*/, as plain
declarations inside their <script type="module">s (one each: their names stay apart), so the page stays one
file.

  web/emu/inline_lib.py IN.html OUT.html"""
import re
import sys
from pathlib import Path

WEB = Path(__file__).resolve().parents[1]


def lib(names):
    out = []
    for name in names:
        src = (WEB / name).read_text()
        if re.search(r"^\s*import\b", src, re.M):
            raise SystemExit(f"inline_lib: {name} imports something: inline that too")
        out.append(f"/* ---- web/{name} (inlined) ---- */\n" + re.sub(r"^export\s+", "", src, flags=re.M))
    return "\n".join(out)


def main(src, dst):
    page = Path(src).read_text()
    for mark, names in (("/*FM1LIB*/", ("fm1pkg.js", "fm1ota.js")), ("/*HP1CONTENT*/", ("hp1content.js",))):
        if page.count(mark) != 1:
            raise SystemExit(f"inline_lib: the page has no {mark} (or more than one)")
        page = page.replace(mark, lib(names).replace("</script", "<\\/script"))
    Path(dst).write_text(page)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
