# esp32idf_APRS — Documentation

Exhaustive, trilingual (English / Español / Italiano) documentation for the
`esp32idf_APRS` firmware, built with [Sphinx](https://www.sphinx-doc.org/) and
the [Breeze](https://pypi.org/project/sphinx-breeze-theme/) theme, ready to
publish on Read the Docs.

## Layout

```
docs/
├── root/index.html      language-chooser landing page ("first page")
├── _shared/conf_base.py shared Sphinx config (theme, extensions, language switcher)
├── en/                  English source tree (conf.py + .rst + _static/)
├── es/                  Spanish source tree (conf.py + .rst + _static/)
├── it/                  Italian source tree (conf.py + .rst + _static/)
├── requirements.txt     Sphinx + Breeze theme pins
├── build_all.py         builds all three trees into ./_site
└── .readthedocs.yaml    Read the Docs build config
```

Each language tree carries its own `_static/` (identical copies of `custom.css`,
`logo.png` and `welcome_logo.png`) since each is an independent Sphinx project
and Sphinx only looks up `html_static_path` inside its own source tree — there
is no shared top-level `_static/` folder. `logo.png` is the small top-bar brand
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

The included `.readthedocs.yaml` builds the English tree as the primary Sphinx
project, then adds the Spanish and Italian trees and the landing page in a
`post_build` job — reproducing the sibling layout the switcher needs. No
dashboard configuration is required beyond importing the repository.

## Requirements

* Python 3.10+
* Sphinx ≥ 8.1, < 9  (Sphinx 9 is not yet compatible with the Breeze theme)
* sphinx-breeze-theme 0.13.x

Both are pinned in `requirements.txt`.
