.. _en-bulletins-objects:

============================
Bulletins, Objects and Items
============================

Two subsystems let the station transmit standing announcements and named map
points of its own. Both keep their state in dedicated LittleFS files rather than
in ``g_config``, to keep the resident configuration small, and both are driven
by the shared beacon scheduler.

Bulletins
=========

``main/bulletins.c`` transmits up to five APRS bulletins. Each bulletin has:

* its own text,
* an addressee **identifier** and **group** name,
* an **RF** and/or **APRS-IS** enable,
* a transmit **interval**,
* an optional **"expire after N hours"** window.

The identifier and group together select which of the three addressee forms
APRS101 chapter 14 defines goes on the air:

.. list-table::
   :header-rows: 1
   :widths: 25 20 55

   * - Form
     - Addressee
     - When
   * - General bulletin
     - ``BLN1``
     - Identifier ``0``–``9``, no group name. Bulletins sharing an identifier
       replace one another on the receiver, so the identifier doubles as a slot
       number for a multi-line bulletin.
   * - Group bulletin
     - ``BLN1WX``
     - Identifier ``0``–``9`` plus a group name of up to five characters. Only
       stations subscribed to that group display it.
   * - Announcement
     - ``BLNQ``
     - Identifier ``A``–``Z`` and no group name. Most client software keeps and
       re-displays announcements far longer than bulletins, which is why the
       spec gives them their own identifier space.

``bulletins_build_addressee()`` normalizes as it builds, so nothing the 9-char
addressee field cannot carry ever reaches the air: an identifier outside
``0``–``9``/``A``–``Z`` falls back to the slot's own digit, the group name is
uppercased and stripped of anything outside ``A``–``Z``/``0``–``9``, and an
announcement identifier suppresses the group name entirely.

An expired bulletin auto-clears its enable flag and leaves the air. Bulletins
persist to their own ``/storage/bulletins.json``. The page is gated by the
``ENABLE_BULLETINS`` compile-time switch.

.. note::

   NTS Radiograms, also described in chapter 14, are a traffic-handling message
   format rather than a bulletin addressee form, and are not produced here.

Objects and Items
=================

``main/objects_items.c`` transmits up to five APRS Objects/Items, each with:

* a **name**, **position** and **symbol**,
* optional **course/speed** and **comment**,
* an **RF** and/or **APRS-IS** enable,
* a **repeat interval** with optional interval decay,
* a **Type** control: Object (timestamped, ``;``) or Item (non-timestamped,
  ``)``).

**Killing** an object transmits it a few extra times (so listeners drop it from
their maps), then auto-disables it. Objects/Items persist to their own
``/storage/objitems.json``. The page is gated by the ``ENABLE_OBJECTS_ITEMS``
compile-time switch.

Permanent objects
------------------

An Object may also be marked **permanent**. A permanent Object is transmitted
with the fixed ``111111z`` pseudo-timestamp defined by ``freqspec.txt``
instead of the live ``DDHHMMz`` time. This is the recommended convention for
voice-repeater frequency objects and similar recurring, station-owned
announcements: a receiving station treats the ``111111z`` stamp as a marker
that the Object must not be replaced by any other station's similarly named
Object, only updated or moved by the same originating station.

The Permanent checkbox only applies to an Object; it has no effect on an Item,
which never carries a timestamp of any kind.

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
