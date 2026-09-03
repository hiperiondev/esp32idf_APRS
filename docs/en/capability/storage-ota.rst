.. _en-storage-ota:

===============
Storage and OTA
===============

LittleFS
========

The 512 KB ``storage`` partition is mounted as **LittleFS** at ``/storage``
(``main/storage.c``). On first boot it auto-formats. It holds every persistent
file the firmware writes:

* ``/storage/config.json`` — the resident configuration (see
  :ref:`en-configuration`).
* ``/storage/telemetry.json`` — telemetry channel-0 config.
* ``/storage/bulletins.json`` — the five bulletins.
* ``/storage/objitems.json`` — the five objects/items.
* ``/storage/telegram.json`` — the Telegram bot's whole configuration.
* ``/storage/winlink.json`` — the replies the Winlink service has sent back.

The *Storage* web page is a full LittleFS browser: it lists files with sizes,
downloads (``GET /download?file=…``), deletes (``POST /delete`` with the
filename in the form body), accepts
multipart uploads (``/upload``), reports usage and can reformat the volume
(``/format``).

Why LittleFS and not SPIFFS
===========================

Although the partition sub-type is ``spiffs`` (a partition-table label), the
volume is mounted with the ``joltwallet/littlefs`` component. LittleFS is
power-loss resilient and wear-levelled, which matters for a device that writes
its configuration on every settings save.

Atomic, heap-friendly saves
===========================

Every JSON file is written by a small streaming, token-at-a-time writer rather
than by building a full cJSON tree and then serialising it — because that would
need the tree **and** its serialised buffer alive at once on a small, fragmented
heap. Instead the writer streams straight to the file. Each save is **atomic**:
it writes ``<file>.tmp`` and then renames. Every writer also calls ``setvbuf()``
immediately after ``fopen()`` so newlib does not lazily allocate a large stdio
buffer mid-write, which on a fragmented heap is a subtle source of intermittent
double-exception crashes. The buffer they install is one shared 512-byte static
object defined in ``main/json_store.c``: the filesystem-wide writer gate
(``storage_write_lock()``) means only one save can be in flight at a time, so a
single buffer serves all six stores.

Loading is done with **cJSON**; missing or corrupt files fall back to defaults
that are then immediately saved, so each file always exists and is consistent.
Unknown keys in an existing file are ignored, so older config files still load.

OTA firmware update
===================

The partition table provides two app slots (``ota_0`` / ``ota_1``), which
enables OTA update from the web admin's **About / Firmware** page:

#. The operator picks a ``.bin`` and uploads it (``POST /ota_update``).
#. It is streamed straight into the inactive slot via ``esp_ota_write()`` —
   never buffered whole in RAM — with a progress bar.
#. Once written and verified, the device reboots into the new slot.

**Automatic rollback.** ``CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`` is on, so a
freshly flashed image boots in "pending verify" state. The firmware confirms the
image only after NVS/LittleFS mounted, Wi-Fi came up and the web admin is
listening (``esp_ota_mark_app_valid_cancel_rollback()`` in ``main.c``). A bad
image that never reaches that bar is rolled back to the previous slot
automatically on the next reset.

