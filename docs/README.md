# esp32idf_APRS — Documentation

Exhaustive, trilingual (English / Español / Italiano) documentation for the
`esp32idf_APRS` firmware, built with [Sphinx](https://www.sphinx-doc.org/) and
the [Breeze](https://pypi.org/project/sphinx-breeze-theme/) theme, ready to
publish on Read the Docs.

## Layout

```
esp32_APRS_igate/
├── .readthedocs.yaml    Read the Docs build config (repo root, not docs/)
└── docs/
    ├── root/index.html      language-chooser landing page ("first page")
    ├── _shared/conf_base.py shared Sphinx config (theme, extensions, language switcher)
    ├── en/                  English source tree (conf.py + .rst + _static/)
    ├── es/                  Spanish source tree (conf.py + .rst + _static/)
    ├── it/                  Italian source tree (conf.py + .rst + _static/)
    ├── schematics/          radio-interface schematics (TX, RX, PTT) shared by the
    │                        three hardware.rst pages through ../schematics/
    ├── dataflow/, tuning/   master copies of the per-language data-flow and tuning
    │                        figures (each tree embeds its own copy from _static/)
    ├── _static/             master copies of custom.css and logo.png (not read by Sphinx)
    ├── requirements.txt     Sphinx + Breeze theme pins
    └── build_all.py         builds all three trees into ./_site
```
Each language tree carries its own `_static/` (identical copies of `custom.css`,
`logo.png` and `welcome_logo.png`, plus that language's `dataflow/` and `tuning/`
figures) since each is an independent Sphinx project and Sphinx only looks up
`html_static_path` inside its own source tree. The top-level `docs/_static/`,
`docs/dataflow/` and `docs/tuning/` folders are only the master copies those
trees are refreshed from; no build reads them, so an edited image or stylesheet
must be copied into each language's `_static/` to take effect. `logo.png` is the small top-bar brand
logo (set via `light_logo`/`dark_logo` in `_shared/conf_base.py`);
`welcome_logo.png` is the larger centered image at the top of each language's
Welcome page (`index.rst`). The two are independent by design.

Each language tree is a complete, independent Sphinx project. The three are
linked as **siblings** by the per-page language switcher (the globe menu in the
Breeze top bar): from any page in one language you can jump straight to the same
page in another. The `root/index.html` landing page lets a first-time visitor
pick a language.

The chapters are organised by **Functionality** (what the station does),
**Capability** (cross-cutting properties), and **Internals** (how it is built),
plus a **Reference** section.

## Build locally

```bash
python -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
python build_all.py
# open _site/index.html
```

Or build a single language on its own:

```bash
cd en && python -m sphinx -b html . _build   # then open _build/index.html
```

## Publish on Read the Docs

The included `.readthedocs.yaml` points its `sphinx:` block at the English tree
only to satisfy Read the Docs. Its `post_build` job then wipes that output and
rebuilds all three trees into sibling `en/`, `es/` and `it/` folders, placing
the landing page at the root — reproducing the sibling layout the switcher
needs, exactly like `build_all.py`. No dashboard configuration is required
beyond importing the repository.

Both `build_all.py` and the Read the Docs job run Sphinx with `-W`, so any
warning (a broken cross-reference, a missing image, a malformed table) fails
the build. Run `python build_all.py` locally before pushing a documentation
change.

## Requirements

* Python 3.10+
* Sphinx ≥ 8.1, < 9  (Sphinx 9 is not yet compatible with the Breeze theme)
* sphinx-breeze-theme 0.13.x

Both are pinned in `requirements.txt`.
