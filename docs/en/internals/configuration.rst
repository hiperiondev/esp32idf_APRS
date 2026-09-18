.. _en-configuration:

========================
The Configuration Engine
========================

One resident config
===================

A single ``app_config_t g_config`` instance (``main/app_config.c`` /
``app_config.h``) is the live copy every subsystem reads. It is loaded at boot
and edited field-by-field by the web POST handlers. Its fields are grouped by
web-admin page: system/time, "My Station" identity, Wi-Fi, IGate, BrandMeister,
Digipeater, Tracker, Weather, GPS, the AFSK modem, System/HTTP auth, Message,
Query and the Winlink account.

Field names and JSON keys are kept **1:1** with the original reference project's
``config.h``/``config.cpp``, so every value the web admin shows has a home and
an operator moving between the two projects recognises the keys.

One file per functionality
==========================

The resident structure is one thing; where it is stored is another. Each web
admin page that owns persistent settings has **one file of its own** under
``/storage``, named after the page — the sidebar is the index of the file list:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - File
     - Page
   * - ``system.json``
     - System: CPU clock, SNTP hosts and resync, timezone, web admin credentials.
   * - ``station.json``
     - My Station: callsign, position, PHG antenna data, ambiguity, status
       report options, no-archive and DAO switches.
   * - ``wireless.json``
     - Wireless: interface selection, TX power, SoftAP channel/SSID/PSK, the
       five station profiles.
   * - ``radio.json``
     - Radiomodem: AFSK/FSK modulation, FX.25, preamble, TX buffers, CSMA
       timing, PTT hold, duty-cycle limiter.
   * - ``igate.json``
     - IGate: both gating directions and their filters, buddy and satellite
       lists, duplicate cache, range and prefix gates, message gating, the four
       APRS-IS server slots, the beacon and its data extension.
   * - ``brandmeister.json``
     - BrandMeister: the interconnect switches and the gateway callsign list.
   * - ``digi.json``
     - Digipeater: the alias table and repeating rules, the beacon, **and the
       four shared path presets**, which are edited on this page.
   * - ``tracker.json``
     - Tracker: the beacon, Mic-E options and the SmartBeaconing parameters.
   * - ``weather.json``
     - Weather: the report and the per-field sensor mapping.
   * - ``gps.json``
     - GPS: the receiver switch.
   * - ``message.json``
     - Message: the messaging service, retries, alarm GPIO and message groups.
   * - ``winlink.json``
     - Winlink: the account and session settings (the replies are a separate
       file, ``winlink_mail.json``).
   * - ``query.json``
     - Query: the responder switches and the capabilities beacon.

Each row is an ``app_config_section_t`` value, and the ``SECTIONS`` table in
``app_config.c`` names the file and the two halves of its codec. A page's save
handler calls ``app_config_save_section()`` with its own section, so saving the
digipeater never rewrites the IGate's file and a write that fails cannot take
another functionality's settings with it. The *My Station* page is the one that
names several: its "Use My Station Data" mirrors reach into five other
services' fields, so it passes every section it touched to
``app_config_save_sections()`` — a partial save would leave the station's
identity split across files that no longer agree.

The read order matters in exactly one place: the BrandMeister reader re-applies
the worldwide-monitor interlock against the INET→RF gating the IGate file
carries, so the IGate section is read first.

.. note::

   Pages with no persistent settings of their own — Dashboard, Snd/Rcv Msg,
   Console Logs, File Storage, About — have no file. Four subsystems keep
   structures of their own rather than fields of ``app_config_t``, and own
   their files directly: ``/storage/telemetry.json``, ``bulletins.json``,
   ``objitems.json`` and ``telegram.json``. The reason is size — those tables
   would significantly enlarge the resident structure — but the result is the
   same rule: one functionality, one file.

Loading and saving
==================

* **Defaults first, once.** ``app_config_load()`` fills the whole structure
  from ``app_config_set_defaults()`` before opening the first file, then reads
  each section over it. Every section reader takes each key's fallback from the
  very field it is about to overwrite, so a key a file does not carry — and a
  section whose file does not exist at all — keeps its documented default. This
  also keeps a second ``app_config_t`` off the stack of the loading task, which
  already has one section's cJSON tree live in the heap beside it.
* **Loaded** with **cJSON**, one file at a time, so the peak allocation is the
  largest single section rather than the whole configuration.
* **Missing, empty or corrupt** → that section is rewritten from the defaults
  before the load returns. This is what guarantees every functionality has a
  file on flash from the first boot onward, without an operator ever having to
  visit its page.
* **Out of memory** → nothing is written at all and the whole load reports
  failure. ``cJSON_Parse()`` returns ``NULL`` for a bad file and for a failed
  allocation alike, so ``json_store_read()`` rescans the text with a
  non-allocating check before deciding which happened; confusing the two would
  wipe a good configuration on a busy boot. Rewriting the genuinely absent
  sections while another is still unread would be worse still — it would
  persist a configuration assembled half from flash and half from defaults —
  so the pass writes nothing and ``main.c`` retries once before falling back to
  the factory set.
* **Saved** by a small token-at-a-time JSON writer (``jw_t``/``jadd_*``) that
  streams straight to the file, avoiding the double heap allocation a full cJSON
  tree plus its serialised buffer would need. A static ``setvbuf()`` buffer is
  installed right after ``fopen()`` so newlib does not lazily allocate a large
  stdio buffer mid-write.
* **Atomic**, per file: write ``<name>.json.tmp``, then rename. A multi-section
  save takes the filesystem-wide writer gate once around the whole run, and
  attempts every selected file even after one has failed, so a full filesystem
  does not leave the rest holding settings the operator has already replaced.

Public API: ``app_config_set_defaults()``, ``app_config_load()``,
``app_config_save_section()``, ``app_config_save_sections()``,
``app_config_save()`` (every section), ``app_config_section_path()``,
``app_config_factory_reset()``, and the live instance
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
   ENABLE_BRANDMEISTER ENABLE_QUERY        ENABLE_DIGIPEATER   ENABLE_TRACKER
   ENABLE_WEATHER      ENABLE_TELEMETRY    ENABLE_GPS          ENABLE_TELEGRAM
   ENABLE_WINLINK      ENABLE_LOGS         ENABLE_SYSTEM       ENABLE_WIRELESS
   ENABLE_FILE_STORAGE ENABLE_ABOUT_FIRMWARE

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
   ITEM 1<<5 · QUERY 1<<6 · BUOY 1<<7 · POSITION 1<<8 · OTHER 1<<9

``IGATE_FILT_OTHER`` is the one bit that covers several payload kinds at once —
station capabilities, user-defined formats, Agrelo direction finding, Maidenhead
locator beacons and the reserved map feature — which is why the *Filter*
fieldsets render nine checkboxes for ten bits. ``IGATE_FILT_QUERY`` has no
checkbox of its own either: queries are classified so the gating code can name
them, but the operator governs them from the Query page instead. Third-party
traffic and test data sit outside every bit and are never relayed on the
strength of the mask.
