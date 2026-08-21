.. _en-localization:

============
Localization
============

The web admin is fully localized into **English, Spanish and Italian**. The
model is **one language per firmware image** — there is no runtime language
switch, and no other language's strings are compiled into a given build.

.. note::

   This applies to the *firmware's* web admin. *This documentation* is separately
   available in all three languages at once, with a per-page language switcher —
   that is a property of the docs, not the firmware.

How it works
============

* ``app_config.h`` defines the language codes ``LANG_EN 0``, ``LANG_ES 1``,
  ``LANG_IT 2`` and the active ``LANGUAGE`` (default ``LANG_EN``).
* ``translations/translations.h`` is the **only** place that decides which
  ``lang_xx.h`` gets included, via ``#if LANGUAGE == …`` branches.
* Every user-visible string in the web admin goes through a ``TR_xxx`` macro.
  There are 649 such macros, and each of the three ``lang_*.h`` files defines
  all of them.

Selecting the language at build time
====================================

.. code-block:: bash

   idf.py build                      # English (default)
   idf.py build -DLANGUAGE=LANG_ES   # Spanish
   idf.py build -DLANGUAGE=LANG_IT   # Italian

You can also set it in ``CMakeLists.txt`` (``set(LANGUAGE LANG_ES)`` /
``target_compile_definitions``) or change the default in ``app_config.h``.

Adding a language
=================

#. Copy ``translations/lang_en.h`` → ``lang_xx.h``, translate every literal,
   and keep every macro name identical.
#. ``#define LANG_XX <next free number>`` in ``app_config.h``.
#. Add an ``#elif LANGUAGE == LANG_XX`` branch in ``translations.h``.
#. Build with ``-DLANGUAGE=LANG_XX``.

.. important::

   Missing a ``TR_xxx`` in one language is a **compile error in that language's
   build** — intentional, so untranslated strings can never ship silently. This
   is why all three ``lang_*.h`` files always carry exactly the same set of
   macros.
