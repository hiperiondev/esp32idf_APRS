#!/usr/bin/env python3
"""
Build the trilingual esp32idf_APRS documentation into a single deployable site.

Output layout (in ./_site):

    _site/index.html      <- language-chooser landing page
    _site/en/...          <- built English HTML
    _site/es/...          <- built Spanish HTML
    _site/it/...          <- built Italian HTML

The per-page language switcher (the globe menu in the Breeze top bar) links
between the three trees as siblings (../en/, ../es/, ../it/), so this exact
sibling layout is what makes it resolve at runtime.

Usage:

    pip install -r requirements.txt
    python build_all.py

Then open _site/index.html.
"""

import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LANGS = ["en", "es", "it"]
OUT = os.path.join(HERE, "_site")


def main():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)

    # Landing page at the site root.
    shutil.copyfile(
        os.path.join(HERE, "root", "index.html"),
        os.path.join(OUT, "index.html"),
    )

    for lang in LANGS:
        src = os.path.join(HERE, lang)
        dst = os.path.join(OUT, lang)
        print(f"==> building {lang}")
        rc = subprocess.call(
            [sys.executable, "-m", "sphinx", "-b", "html", "-W", src, dst]
        )
        if rc != 0:
            print(f"build for {lang} failed", file=sys.stderr)
            sys.exit(rc)

    print(f"\nDone. Open {os.path.join(OUT, 'index.html')}")


if __name__ == "__main__":
    main()
