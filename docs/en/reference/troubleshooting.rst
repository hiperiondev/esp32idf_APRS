.. _en-troubleshooting:

===============
Troubleshooting
===============

"I switched to Station mode, saved, rebooted, and nothing happens."
===================================================================

Read the boot log — this path is heavily instrumented:

* ``esp_wifi_connect()`` is only legal once the station has *actually* started
  (``WIFI_EVENT_STA_START``). The connect is issued from that handler and every
  attempt logs its result.
* If no Wi-Fi Client slot is **enabled with an SSID**, the firmware dumps every
  slot and tells you which mistake it is ("enabled, but the SSID is EMPTY" vs
  "has an SSID, but 'Enable' is not ticked").
* STA-only with nothing to join falls back to AP+STA so the web admin stays up.

Disconnect reason codes are logged:

.. list-table::
   :header-rows: 1
   :widths: 44 56

   * - Reason
     - Meaning
   * - 15, 204
     - wrong password
   * - 201
     - SSID not visible: wrong name, out of range, or 5 GHz-only
   * - 2 / 8 / 200
     - ordinary roaming / AP-side drops

"AP won't associate at all."
============================

A zeroed ``wifi_config_t`` leaves ``pmf_cfg.capable = false``, and
WPA3 / WPA2-with-PMF-required APs refuse such a station. The firmware sets
*capable, not required*, which works against both old and new APs.

"Boot hangs for ~5 seconds."
============================

Expected: ``modem_init()`` blocks while ``ModemCalibrateSampleRate()`` measures
the real ADC clock. Once per boot.

"Beacons at boot don't transmit."
=================================

Expected: ``aprs_service_start()`` runs before ``modem_init()``, so early
beacons are dropped with a debug log until ``s_modemReady``.

"LOOP TEST fails with 'no packet received back'."
=================================================

Check the ADC attenuation: the DAC swings the full rail while a 0 dB attenuation
only measures ~0–1.1 V, clipping the tone beyond the demodulator's ability to
lock. The component hard-codes ``ADC_ATTEN_DB_12``, which is correct; if you
overrode it, put it back. Also confirm the GPIO25 → GPIO33 loopback wire.

"IGate says unverified."
========================

Wrong ``aprs_mycall`` / ``aprs_passcode``. The banner is logged; so is the exact
login line, including the filter string, so a malformed filter is visible
immediately.

"Everything works but aprs.fi doesn't show my station."
=======================================================

Beacons: enable the position beacon and at least one of ``loc2rf`` /
``loc2inet``, and set real coordinates. Relaying traffic never announces you.

"9600 Bd loses frames."
=======================

That is the pathology the ADC rate, conversion-frame size and core split were
changed to fix (see :ref:`en-dsp-signal-chain`). If you overrode
``MODEM_ADC_SAMPLERATE``, ``MODEM_ADC_CONV_FRAME``, ``MODEM_DAC_TIMER_CORE`` or
``MODEM_ADC_ISR_CORE``, revert them. Also confirm you are feeding
**flat/discriminator** audio.

"The PTT LED stays on when idle."
=================================

The PTT logic is correct; its polarity is a compile-time constant, and the
shipped board definition is ``MODEM_PTT_ACTIVE_HIGH=1`` (active-high) in the
top-level ``CMakeLists.txt``. Active-high means idle/unkeyed drives the pin
**low** and keyed drives it high; active-low is the mirror image, so idle leaves
the pin high and an LED on that pin stays lit. If the LED tracks the opposite of
what you expect, your driver stage inverts (an optocoupler does; a plain NPN
low-side switch does not): flip the macro to the other value and do a full clean
rebuild — the value is baked into ``afsk.c``, so an incremental build will not
pick it up.
"Telegram stops answering after running for a while, with 'mbedtls_ssl_fetch_input' or 'Socket is not connected' in the log."
===============================================================================================================================

The polling path keeps its HTTPS connection to the Telegram API open between
cycles, so a long poll that returns nothing does not pay for a new TLS
handshake every ten seconds. If that connection sits idle long enough, the
peer or an intermediate NAT can close it silently; the socket is then stale
even though nothing local noticed. ``telegram_bot_client_call()`` treats a
transport failure as a sign of exactly that: it force-closes the connection
and retries the request on a freshly opened socket, up to three attempts
total with a delay that grows between them, so a single stale session
recovers on its own within the same call. If the error keeps repeating
across every attempt, the network itself is down rather than one stale
socket; check Wi-Fi/Internet connectivity and the bot token.

"sendMessage fails with 'ESP_ERR_HTTP_CONNECT' right after an update arrives, preceded by 'Dynamic Impl: alloc(...) failed'."
=============================================================================================================================

A fresh TLS handshake asks the heap for its record buffers as single
allocations of a few kilobytes each, so what decides whether it succeeds is
the largest **contiguous** free block, not the total free heap. The ESP-IDF
allocator reports the refusal as ``Dynamic Impl: alloc(...) failed``, mbedTLS
turns it into ``mbedtls_ssl_handshake returned -0x008D`` and the transport
sees ``ESP_ERR_HTTP_CONNECT``.

A live TLS session holds a block of comparable size for as long as it is
kept, so the firmware never holds two at once. The transmit handle runs with
keep-alive off and is therefore empty the moment a call returns, and the
polling connection is released by ``telegram_release_poll_connection()``
immediately before any outgoing request, which is the moment that matters:
a reply is sent right after a batch of updates arrived, with the payload and
the decoded tree still in memory. The poll pays one extra handshake on its
next cycle and nothing else.

Every failed attempt is logged with the free heap and the largest free block
at that instant. If the largest block is comfortably above four kilobytes and
the call still fails, the fault is the link rather than the heap. If it is
not, the device is genuinely short of contiguous memory: lower
``rx_buffer_size`` on the client handles, or reduce what the rest of the
firmware holds at that moment.

``CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN`` is not the lever it looks like. It is a
ceiling on the record the peer may send, and the certificate chain Telegram
presents is around four kilobytes in one record, so lowering it under that
figure does not save memory — it makes the handshake fail outright, on every
attempt and in every heap condition.

"The free heap was lower this morning than it was last night, and the log says nothing about it."
=================================================================================================

Every other heap figure this firmware logs is printed *after* a failure: the
two lines in the Telegram transport, the diagnosis detail attached to a failed
bring-up, the write-error path of the JSON stores. They describe the heap once
the damage was done, and say nothing about how it got there. A watermark that
fell at three in the morning therefore leaves no trace of what was running.

The standing instrumentation that answers such a question lives in
``main/heap_monitor.c``, driven from the APRS service's 1 Hz tick, and is
configured under ``APRS heap instrumentation`` in ``idf.py menuconfig``.

**One line per minute.** ``CONFIG_APRS_HEAP_REPORT`` is on by default and emits
one line every ``CONFIG_APRS_HEAP_REPORT_PERIOD_S`` seconds::

   I (3600123) heap_monitor: free=104512 largest=45056 minimum=41216

Three figures, read together. The free size is how much memory exists; the
largest free block is the biggest single allocation still possible, and the two
drifting apart is a heap breaking up rather than being consumed; the minimum is
the low watermark since boot, so a dip that recovered before the next line
still shows there. They are reported for internal 8-bit memory, the same class
the transport prints on failure, so the two kinds of line can be read against
each other. The line costs three allocator queries per period and no memory.

**Brackets around the handshakes.** ``CONFIG_TELEGRAM_BOT_HEAP_BRACKET``, under
``Telegram bot transport``, logs the same two figures immediately before and
immediately after every request the bot makes — each attempt of a JSON call,
plus the multipart upload and the file download, which open connections of
their own. A TLS handshake asks for its record buffers as single allocations of
a few kilobytes, so it is the largest single event this firmware performs
against the heap. If the notches in the minute-by-minute trace fall inside
these brackets, the handshakes are what move the heap; if they fall between
them, something else does. Off by default: it is two lines per API call and the
polling path makes one every few seconds.

**Attributing the memory to a task.** Enable ``CONFIG_HEAP_TASK_TRACKING``
(``Component config`` → ``Heap memory debugging``) and
``CONFIG_APRS_HEAP_REPORT_TASKS`` appears, adding the per-task overview table
under each heap line, so a genuine leak names its owner in one dump instead of
a week of bisection. Tracking costs RAM per live allocation and slows every
allocation and free, so it belongs in a diagnostic build — turn it off again
afterwards.

**Ruling out corruption.** ``CONFIG_APRS_HEAP_INTEGRITY_CHECK`` sweeps every
heap every ``CONFIG_APRS_HEAP_INTEGRITY_PERIOD_S`` seconds and logs an error,
after the addresses the checker itself prints, if anything is wrong. Corrupted
allocator structures present as inexplicable heap behaviour and are otherwise
chased as a leak. The sweep holds each heap's lock while it walks it, so other
tasks block if they allocate meanwhile — hence off by default and on a slow
timer when on. What it can see depends on the corruption detection level: with
the default (no poisoning) only the allocator's own structures are checked;
select "Light impact" or "Comprehensive" to also verify the canary bytes around
every allocated block.

**Serializing the two heaviest network operations.** The same module also
owns a small non-blocking lock, unrelated to the sampling above and always
present regardless of which ``CONFIG_APRS_HEAP_*`` options are enabled. The
Telegram bot's TLS handshake (:ref:`en-telegram`) and the APRS-IS uplink's TCP
connect (:ref:`en-igate`) each take it around their own heap-floor check and
release it once that setup work is done, so the two never run at the same
instant and compete for the same contiguous memory. A caller that finds the
lock already held simply defers to its own next retry pass — nothing here
blocks waiting for the other side.

"Menu buttons keep spinning and the log shows 'query is too old and response timeout expired or query ID is invalid'."
======================================================================================================================

Telegram invalidates a callback query a few seconds after the button is
pressed. Answering it is a request of its own, and on this device a request
can cost a TLS handshake, so the order in which the work is done decides
whether the answer still arrives in time.

Three things keep it inside the deadline. The query is answered before the
button handler runs, not after it, so building and sending a report never
delays the answer. The transmit connection stays open across one batch of
updates, so a burst of presses pays one handshake between them all instead
of one each. And a single failed polling cycle no longer adds its own five
second pause on top of the retries the transport already spent, because that
pause is time the queued queries spend ageing; the pause returns as soon as
failures repeat, which is when the network really is down.

A query that is genuinely too old is refused by Telegram with a 400 and the
message above, and the batch it belonged to is still processed. If this
appears once after a poll failure or a reconnection, the queue simply
outlived its updates. If it appears steadily, the device is not keeping up
with the poll at all: look for the polling failures above it in the log.

"Requests fail at random with 'mbedtls_ssl_handshake returned -0x2700', while free heap is plentiful."
======================================================================================================

``-0x2700`` is ``MBEDTLS_ERR_X509_CERT_VERIFY_FAILED``: the TLS handshake
reached the server, exchanged messages and then refused the certificate it
was shown. Nothing was wrong with the link and nothing was wrong with the
heap, which is why the figures printed alongside the failure look healthy.

``api.telegram.org`` is answered by more than one front-end and they do not
all chain to the same certificate authority. When the transport validates
against a PEM file rather than the ESP-IDF bundle, that file only trusts the
authorities it actually carries, so a file holding a single root validates
the connections that land on a matching front-end and fails the others.
Which front-end DNS hands out varies between attempts, which is exactly why
the failure looks random and why a retry usually succeeds.

The transport reports it explicitly. A refused chain is logged as ``Peer
certificate refused, verification flags 0x…, validating against <path>``,
and start-up logs how many anchors the file yielded (``Loaded N trust
anchors from …``). One anchor with intermittent ``-0x2700`` is the signature
of this problem.

There are two fixes. Concatenate the missing roots into the PEM file — every
certificate in it becomes a trust anchor, and the file is replaceable from
the web admin's File Storage page without rebuilding. Or select
``TELEGRAM_BOT_CERT_BUNDLE`` in menuconfig and validate against the
certificate bundle shipped with ESP-IDF, which covers the public authorities
and keeps working when Telegram rotates its chain, at the cost of carrying
the bundle in the image.

Note that ``CONFIG_MBEDTLS_HAVE_TIME_DATE`` is not enabled in this firmware,
so certificate validity dates are not checked. An unsynchronised clock is
therefore never the cause of ``-0x2700`` here.
