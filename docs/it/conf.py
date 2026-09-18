# -*- coding: utf-8 -*-
import os
import sys

# Resolve the shared-config directory relative to THIS file, not the current
# working directory — so the build works whether Sphinx is invoked from inside
# this folder (local: `cd it && sphinx-build`) or from the repository root
# (Read the Docs: `sphinx-build it ...`).
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from _shared.conf_base import apply_base  # noqa: E402

apply_base(globals(), language="it", project_suffix="Italiano")
