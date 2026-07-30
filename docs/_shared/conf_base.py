# -*- coding: utf-8 -*-
"""
Shared Sphinx configuration base for the esp32idf_APRS documentation.

Each language sub-tree (en/, es/, it/) has its own tiny conf.py that sets the
language code and the human labels, then calls apply_base(globals()) to pull in
everything common: the Breeze HTML theme, the extensions, and the language
switcher wired into the theme's top navigation.

This keeps the three language builds byte-for-byte identical except for the
translated content and the active-language marker, which is exactly what a
ReadTheDocs multi-language build wants.
"""

import datetime


# ---------------------------------------------------------------------------
# The three languages this documentation ships in. The order here is the order
# they appear in the top-of-page language switcher and on the landing page.
# ---------------------------------------------------------------------------
LANGUAGES = [
    # (code, native label, English label, relative path from one lang root to
    #  another — they are siblings, so it is always "../<code>/")
    ("en", "English", "English"),
    ("es", "Español", "Spanish"),
    ("it", "Italiano", "Italian"),
]


def apply_base(ns, *, language, project_suffix):
    """Populate a language conf.py's globals() with the shared settings.

    :param ns: the calling conf.py's globals() dict.
    :param language: "en" | "es" | "it".
    :param project_suffix: short human language name shown after the project
        title in the browser tab, e.g. "English".
    """
    year = datetime.datetime.now().year

    ns["project"] = "esp32idf_APRS"
    ns["author"] = "Emiliano Augusto González (LU3VEA)"
    ns["copyright"] = f"{year}, Emiliano Augusto González — GNU GPL v3"
    ns["release"] = "work-in-progress"
    ns["version"] = "wip"
    ns["language"] = language

    # -- General ------------------------------------------------------------
    ns["extensions"] = [
        "sphinx.ext.todo",
        "sphinx.ext.githubpages",
    ]
    ns["templates_path"] = ["_templates"]
    ns["exclude_patterns"] = ["_build", "Thumbs.db", ".DS_Store"]
    ns["source_suffix"] = {".rst": "restructuredtext"}
    ns["master_doc"] = "index"
    ns["numfig"] = True
    ns["today_fmt"] = "%Y-%m-%d"

    # -- HTML output --------------------------------------------------------
    ns["html_theme"] = "breeze"
    ns["html_static_path"] = ["_static"]
    ns["html_css_files"] = ["custom.css"]
    ns["html_title"] = f"esp32idf_APRS — {project_suffix}"
    ns["html_short_title"] = "esp32idf_APRS"
    ns["html_last_updated_fmt"] = "%Y-%m-%d"
    ns["html_show_sourcelink"] = False

    # Breeze theme options (only keys the theme actually accepts — see its
    # theme.conf). The GitHub repo the header "stars/repo" widget points at is
    # configured through html_context (github_user/github_repo) below, not here.
    #
    # header_tabs=False is deliberate: with it True, Breeze puts the top-level
    # toctree sections (Overview / Functionality / Capability / Internals /
    # Reference) as TABS in the upper header, and the left sidebar then only
    # shows the children of the currently-active tab (toctree level=1). With it
    # False, there are no header tabs and the left vertical sidebar renders the
    # WHOLE toctree (level=0) — every section and every page at once — which is
    # what we want here.
    ns["html_theme_options"] = {
        "default_mode": "auto",
        "header_tabs": False,
        "page_actions": True,
        # Top-bar brand logo (left-most element in the header, before the
        # project title). Breeze looks this file up under html_static_path
        # (i.e. "<lang>/_static/logo.png"), so a copy is kept in every
        # per-language _static/ dir alongside custom.css. Same file for both
        # modes since the logo itself has no light/dark variants; the theme's
        # own header-brand.css caps it at `height: 2rem` so it can never
        # enlarge the top bar regardless of the source image's resolution.
        #
        # NOTE: this is deliberately a *different* file from
        # "<lang>/_static/welcome_logo.png", which each index.rst embeds as
        # the large centered image at the top of the Welcome page body. The
        # two are independent on purpose: changing one must never change
        # the other.
        "light_logo": "logo.png",
        "dark_logo": "logo.png",
    }

    # -- Language switcher --------------------------------------------------
    # Breeze's own header component (components/lang-switcher.html, already in
    # the default header_end) renders html_context["languages"] as a dropdown.
    # It expects each entry as a 3-tuple:
    #     (display_label, url_pattern, lang_code)
    # and calls the theme helper lang_link(url_pattern), which does a NAIVE
    # string substitution of the current page name for "%s" in the pattern —
    # it does NOT know how deeply nested the current page is, so it cannot
    # compute how many "../" are needed to climb back out of the language
    # tree. A fixed pattern such as "../<code>/%s.html" is only correct for
    # pages that live directly under the language root (overview, hardware,
    # getting-started); for anything one level deeper — every page under
    # functionality/, capability/, internals/ and reference/, which is most
    # of the site — a single "../" lands INSIDE the current language tree
    # (e.g. from en/functionality/igate.html it resolves to the nonexistent
    # en/es/functionality/igate.html) instead of at the sibling language root
    # (es/functionality/igate.html). That was the original bug.
    #
    # Fix: don't rely on the theme's static %s pattern at all. Recompute
    # html_context["languages"] on every page via the html-page-context
    # event, using Sphinx's own relative_uri() to work out exactly how many
    # "../" the CURRENT page needs before "<code>/<same-page>.html" — the
    # same helper Sphinx uses internally for pathto(). This is correct at
    # any nesting depth and does not depend on the theme's substitution
    # logic at all.
    def _inject_lang_switcher(app, pagename, templatename, context, doctree):
        # Depth of the current page below its language root: "index" or
        # "overview" -> 0, "functionality/igate" -> 1, and so on. That is
        # exactly how many "../" are needed to reach the language root
        # (en/, es/, it/), plus one more "../" to reach their common parent
        # (the site root) before stepping into the sibling language tree.
        depth = pagename.count("/")
        up = "../" * (depth + 1)

        langs = []
        for code, native, _english in LANGUAGES:
            if code == language:
                langs.append((native, "#", code))
            else:
                target = f"{up}{code}/{pagename}.html"
                langs.append((native, target, code))
        context["languages"] = langs

        # The theme's lang_link() would otherwise run our already-resolved
        # target back through its own "%s" substitution and corrupt it, so
        # replace it with a pass-through for these entries.
        context["lang_link"] = lambda pattern: pattern

    def setup(app):
        # priority > the theme's default (500) so this runs AFTER the
        # theme's own html-page-context handler has already set
        # context["lang_link"] — we then overwrite both languages and
        # lang_link with the corrected versions.
        app.connect("html-page-context", _inject_lang_switcher, priority=900)

    ns["setup"] = setup

    ns.setdefault("html_context", {})
    ns["html_context"]["github_user"] = "hiperiondev"
    ns["html_context"]["github_repo"] = "esp32idf_APRS"
    ns["html_context"]["github_version"] = "master"
    ns["html_context"]["doc_path"] = "docs"
