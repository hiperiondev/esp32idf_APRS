.. _en-bulletins-objects:

============================
Bulletins, Objects and Items
============================

Two subsystems let the station transmit standing announcements and named map
points of its own. Both keep their state in dedicated LittleFS files rather than
in ``g_config``, to keep the resident configuration small, and both are driven
by the shared beacon scheduler.

Bulletins (BLN1..BLN5)
======================

``main/bulletins.c`` transmits up to five APRS bulletins, addressed ``BLN1``
through ``BLN5``. Each bulletin has:

* its own text,
* an **RF** and/or **APRS-IS** enable,
* a transmit **interval**,
* an optional **"expire after N hours"** window.

An expired bulletin auto-clears its enable flag and leaves the air. Bulletins
persist to their own ``/storage/bulletins.json``. The page is gated by the
``ENABLE_BULLETINS`` compile-time switch.

Objects and Items
=================

``main/objects_items.c`` transmits up to five APRS Objects/Items, each with:

* a **name**, **position** and **symbol**,
* optional **course/speed** and **comment**,
* an **RF** and/or **APRS-IS** enable,
* a **repeat interval** with optional interval decay,
* a YAAC-style **"permanent"** flag: permanent → a non-timestamped Item,
  otherwise a timestamped Object.

**Killing** an object transmits it a few extra times (so listeners drop it from
their maps), then auto-disables it. Objects/Items persist to their own
``/storage/objitems.json``. The page is gated by the ``ENABLE_OBJECTS_ITEMS``
compile-time switch.

Why separate JSON files
=======================

Both subsystems, like telemetry, keep page-specific state that would
significantly enlarge the resident ``app_config_t`` (and therefore every
``config.json`` save, which runs against a small, fragmented heap). Keeping
them in their own files means the resident config stays lean and each
subsystem's save touches only its own data. Each file is written with the same
streaming, byte-at-a-time JSON writer the main config uses, under its own mutex,
with an explicit ``setvbuf()`` so newlib does not lazily allocate a large stdio
buffer mid-write on a fragmented heap.
