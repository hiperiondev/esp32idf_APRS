.. _en-telemetry:

=========
Telemetry
=========

The ``telemetry`` subsystem (``main/telemetry.c``) gathers analog and digital
channels from the ``sensors_local`` registry and beacons a standard APRS
Telemetry Data Report (``T#nnn``) on RF and/or APRS-IS, together with the
PARM/UNIT/EQNS/BITS metadata messages that label those channels for receiving
stations. It mirrors the pattern the weather subsystem uses, but for telemetry.

Separate storage
================

Unlike most settings, telemetry configuration deliberately does **not** live in
``g_config``/``config.json``. It persists to its own small LittleFS file,
``/storage/telemetry.json``, the same way bulletins and objects/items keep
their own files. On first boot, or whenever the file is missing, an empty
default set is created so ``/storage/telemetry.json`` always exists once the
subsystem has started. The full schema is ``telemetry_config_t``
(``main/include/telemetry.h``).

Channels
========

Per APRS101 chapter 13, a telemetry report carries:

* **5 analog channels** ``A1``–``A5`` (``TLM_CH = 5``).
* **8 digital bits** ``B1``–``B8`` (``TLM_BIT_NUM = 8``).

Each analog channel has an enable flag, a source sensor-channel index
(``tlm_ana_channel[]``, ``0xFF`` = none), a quadratic calibration
(``value = a·x² + b·x + c``), an expected raw-input range that bounds the
transmitted value, and a decimal-place count. Each digital bit has an enable flag, a source channel, a sense
(Normal / Inverted), per-bit RF/INET routing, and an operator-facing label used
in the BITS message.

What goes on-air
================

``build_tlm_data_packet()`` (in ``telemetry.c``) resolves each mapped channel
from the registry (via ``sensors_local_save_one()``) once per second and
encodes the periodic data report:

.. code-block:: text

   T#sss,a1,a2,a3,a4,a5,bbbbbbbb

The analog fields carry the **raw** sensor reading, clamped to the channel's
declared raw range and written with the per-channel field width and decimals;
the eight ``b`` characters are the digital bits. The calibration is *not*
applied here — APRS101 splits report from metadata, so the ``EQNS.`` message
carries the a/b/c coefficients and each receiving station recovers the
engineering value itself. The report **never** carries channel names — per the
APRS spec, names, units and equations travel separately.

The metadata messages
=====================

At a slower cadence (``info_interval``), the module emits the definition
messages as APRS messages addressed back to the station itself:

.. code-block:: text

   :MYCALL   :PARM.<analog names>,<bit names>
   :MYCALL   :UNIT.<analog units>,<bit on-state labels>
   :MYCALL   :EQNS.<a,b,c per analog channel>
   :MYCALL   :BITS.<bit sense bitmap>,<project title>

Generation of each is individually toggleable (``gen_parm``, ``gen_unit``,
``gen_eqns``, ``gen_bits``).

Report parameters
=================

The configuration also carries APRS101 chapter-13 framing options: a free-text
digipeater path (``report_path``), destination TOCALL (``tocall``),
auto-incrementing sequence number (``auto_seq``), analog field width
(``field_width``), an option to omit unused trailing channels
(``omit_trailing``), a trailing free-text comment (``trail_comment``), and the
number of analog/digital channels actually sent (``analog_count`` /
``digital_count``).

Setting ``field_width`` to 3 zero-pads each analog value to three digits,
000-999 - the range APRS 1.2 allows for this field, extended from the
original APRS101 000-255 window. A channel whose receiving station still
expects the older 0-255 range can be kept inside it by setting that channel's
``ana_raw_min``/``ana_raw_max`` accordingly.

Web page pickers
================

The *Telemetry* page (``page_tlm.c``) populates a *Source* dropdown for each
analog channel and a *Channel* dropdown for each digital bit from the live
``sensors_local`` registry, filtered by each driver's advertised telemetry
channels. Live per-channel values are shown via ``/tlm/values``. See
:ref:`en-sensor-framework`.
