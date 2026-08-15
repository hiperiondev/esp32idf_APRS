.. _en-configuration:

========================
The Configuration Engine
========================

One resident config
===================

A single ``app_config_t g_config`` instance (``main/app_config.c`` /
``app_config.h``) is the live copy every subsystem reads. It is loaded at boot
and edited field-by-field by the web POST handlers. Its fields are grouped by
web-admin page: system/time, "My Station" identity, Wi-Fi, IGate, Digipeater,
Tracker, Weather, the AFSK modem, System/HTTP auth, Message and Query.

Field names and JSON keys are kept **1:1** with the original reference project's
``config.h``/``config.cpp``, so every value the web admin shows has a home and
old ``config.json`` files load unchanged.

.. note::

   Telemetry, bulletins and objects/items configuration deliberately do **not**
   live in ``g_config``. They persist to their own LittleFS files
   (``/storage/telemetry.json``, ``bulletins.json``, ``objitems.json``) to keep
   the resident config — and therefore every ``config.json`` save — small.

Loading and saving
==================

* **Loaded** with **cJSON**. If the file is missing or corrupt, defaults are
  applied **and immediately saved**, so the file always exists and is
  consistent.
* **Saved** by a small token-at-a-time JSON writer (``jw_t``/``jadd_*``) that
  streams straight to the file, avoiding the double heap allocation a full cJSON
  tree plus its serialised buffer would need. A static ``setvbuf()`` buffer is
  installed right after ``fopen()`` so newlib does not lazily allocate a large
  stdio buffer mid-write.
* **Atomic**: write ``/storage/config.json.tmp``, then rename.

Public API: ``app_config_set_defaults()``, ``app_config_load()``,
``app_config_save()``, ``app_config_factory_reset()``, and the live instance
``extern app_config_t g_config``.

Concurrency: the config lock
============================

``g_config`` is written field-by-field by the web POST handlers (a single
settings save rewrites many fields, several of them strings/arrays, one at a
time) while long-running tasks (beacon builders, IGate login, digipeater,
message, weather, query responder) read those same fields. A reader that samples a string
mid-``strcpy`` can see a torn or transiently non-NUL-terminated value and walk
off the end of the buffer. ``app_config_lock()`` / ``app_config_unlock()``
serialise those two sides.

It is a strict **leaf lock**: held only long enough to copy the needed fields
into locals — never across a blocking call, I/O, transmit or another lock.
Scalar (single-word) fields are word-atomic on this MCU and may be read
lock-free. It is distinct from the internal save mutex (held across the whole
flash serialisation).

Compile-time module switches
============================

``app_config.h`` defines a set of ``ENABLE_*`` macros; commenting one out
removes its sidebar entry and its page from the image:

.. code-block:: c

   ENABLE_DASHBOARD    ENABLE_MSG_CHAT     ENABLE_BULLETINS    ENABLE_OBJECTS_ITEMS
   ENABLE_STATION      ENABLE_RADIO_MODEM  ENABLE_MESSAGE      ENABLE_IGATE
   ENABLE_DIGIPEATER   ENABLE_TRACKER      ENABLE_WEATHER      ENABLE_TELEMETRY
   ENABLE_SYSTEM       ENABLE_WIRELESS     ENABLE_FILE_STORAGE ENABLE_ABOUT_FIRMWARE
   ENABLE_QUERY

There is **no** ``ENABLE_SENSORS`` switch: the ``sensors_local`` framework has no
compile-time disable and is always built in (its individual drivers are gated by
their own ``CONFIG_SENSORS_LOCAL_*_DRIVER`` Kconfig options).

Path presets and bitmasks
=========================

Each service (tracker / igate / digi / wx / …) stores a **bitmask**, not a path
string. Bit *N* selects ``g_config.path[N]``, one of the four free-text presets
edited on the *Digi* page. ``aprs_path_build_suffix()`` concatenates every
selected non-empty slot; selected-but-empty slots are skipped. It is shared by
the beacons, weather, telemetry, messages and query answers, and enforces the
AX.25 8-via limit at transmit time, so a configuration that reached the device
without passing through a web form cannot put an over-long path on the air.

Objects/Items are the one service that does not join the slots together: their
proportional pathing sends **one** preset per transmission and rotates through
the selection, so ``objitem_paths()`` builds the list itself. The hop limit binds
per preset there rather than across the selection, and it is counted with the
same ``app_config_path_hop_count()`` the shared builder and the save-time clamp
use — a preset that is over the limit on its own is dropped from the rotation.

Every selector ships selecting preset 0 and nothing else
(``PATH_PRESET_MASK_DEFAULT``), because ``g_config.path[0]`` is the only slot
with a factory string (``WIDE1-1,WIDE2-1``) and a bit pointing at an empty slot
would beacon with a bare destination call.

The IGate filter bits (shared by ``rf2inetFilter`` and ``inet2rfFilter``):

.. code-block:: text

   MESSAGE 1<<0 · STATUS 1<<1 · TELEMETRY 1<<2 · WEATHER 1<<3 · OBJECT 1<<4
   ITEM 1<<5 · QUERY 1<<6 · BUOY 1<<7 · POSITION 1<<8
