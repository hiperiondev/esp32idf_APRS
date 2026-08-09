:orphan:

.. _en-index:

.. image:: /_static/welcome_logo.png
   :align: center
   :width: 300px

.. raw:: html

   <h1 style="text-align: center;">esp32idf_APRS</h1>
   <h2 style="text-align: center;">A complete APRS station on a single ESP32<br>Native ESP-IDF, no Arduino.</h2>
   <p style="text-align: center;"><strong>IGate · Digipeater · Tracker · Weather · Telemetry, with a built-in web admin, an on-chip AFSK/FSK soft-modem, APRS-IS uplink, a runtime sensor-driver framework and OTA firmware updates.</strong></p>

=======
Welcome
=======

Welcome to the documentation for **esp32idf_APRS** — a native ESP-IDF (C, no
Arduino) APRS IGate, Digipeater, Tracker, Weather and Telemetry station that
runs entirely on a single ESP32 microcontroller.

What this project is, in plain terms
=====================================

Most APRS stations you'll find in the field are built from two separate
pieces: a computer (or single-board computer) running APRS software such as
Direwolf or Xastir, plus a hardware or software TNC listening to a radio.
``esp32idf_APRS`` collapses that whole stack — the audio modem, the packet
engine, the gateway logic, the digipeater, and the operator interface — into
the firmware of a single ESP32, with no PC, no Raspberry Pi, and no external
sound card in the loop.

The ESP32's own analog-to-digital converter listens to a radio's speaker or
discriminator output and the firmware demodulates AFSK or FSK audio in
software, entirely on-chip. From there it decodes AX.25 packets, decides
whether to gate them to the internet, digipeat them back out over RF, or
both, and can just as easily originate its own traffic: position beacons,
weather reports, telemetry, messages, bulletins and objects. All of this is
configured through a web page served by the device itself, over Wi-Fi, from a
phone or laptop browser — there is no serial terminal, no special software to
install, and no firmware rebuild needed for day-to-day settings changes.

In short, one small, inexpensive board plus a simple audio interface to a
radio becomes a complete, standalone APRS IGate, digipeater and tracker that
you configure from a browser and then leave running.

What is APRS?
=============

**APRS — the Automatic Packet Reporting System** — is an amateur radio
digital protocol for the real-time, many-to-many exchange of small pieces of
tactical information: a station's position, its status, short text messages,
weather readings, telemetry values, and general-purpose announcements. Unlike
a point-to-point data link, APRS is fundamentally a *broadcast* system —
anyone listening on the shared frequency, or watching the network on the
internet, sees the traffic as it happens.

A short history
----------------

APRS was created by Bob Bruninga, callsign **WB4APR**, a senior research
engineer at the United States Naval Academy. His earliest position-mapping
experiments date back to 1982 on an Apple II, and by 1984 he had a
purpose-built version — the Connectionless Emergency Traffic System — running
on a Commodore VIC-20 to track horses and riders during a 100-mile endurance
event. Through the late 1980s the software moved to the IBM PC and, once
combined with the AX.25 packet-radio protocol and, later, affordable GPS
receivers, it grew into a general real-time tactical reporting system. It was
formally presented to the amateur radio community and named APRS — an
acronym built from Bruninga's own callsign — in a 1992 paper at the ARRL's
Computer Networking Conference. Bruninga continued to maintain the protocol
and its reference site until his death in 2022, after which the APRS
Foundation was formed to carry the protocol forward.

Two developments turned APRS from a niche packet-radio experiment into a
widely used amateur radio service: cheap GPS made automatic, continuous
position reporting practical, and the emergence of **APRS-IS** — the
internet-connected backbone of receiving stations (IGates) that forward RF
traffic onto the public internet — meant that a station's activity could be
seen worldwide within seconds, not just by anyone in radio range.

Current usage
--------------

Today APRS is used worldwide for vehicle and hiker tracking, weather station
reporting, short-range text messaging between operators, event and net
support, search-and-rescue logistics, and simply monitoring who is on the air
locally. Traffic is exchanged over a shared VHF frequency in each region
(commonly 144.390 MHz in North America; other frequencies apply elsewhere),
locally relayed by **digipeaters**, and bridged to the global **APRS-IS**
internet network by **IGates** — the same two roles this firmware
implements. Aggregator sites such as `aprs.fi <https://aprs.fi>`__ let anyone
watch that global traffic on a map in a browser. Many commercial amateur
radios now ship with APRS built in, and a large ecosystem of open-source
software — covered in detail at the end of the next page — has grown up
around the protocol on desktop, mobile and embedded platforms alike.

How this documentation is organised
=====================================

This documentation is organised into three parts, each with its own set of
chapters:

* **Functionalities** — what the station *does* as seen by an operator:
  gatewaying, digipeating, beacons, messaging, weather, telemetry, bulletins,
  objects and the web admin.
* **Capabilities** — the *properties* of the firmware that cut across
  features: the modem profiles, filtering, localization, storage, OTA,
  networking and hardware support.
* **Internals** — how it is *built*: the boot sequence, task map, data flow,
  the DSP signal chain, the configuration engine, and the sensor registry.

.. toctree::
   :maxdepth: 2
   :caption: Getting Started

   hardware
   getting-started

.. toctree::
   :maxdepth: 2
   :caption: Functionalities

   functionality/igate
   functionality/digipeater
   functionality/beacons
   functionality/messaging
   functionality/query
   functionality/weather
   functionality/telemetry
   functionality/bulletins-objects
   functionality/web-admin

.. toctree::
   :maxdepth: 2
   :caption: Capabilities

   capability/modem
   capability/filtering
   capability/networking
   capability/storage-ota
   capability/localization

.. toctree::
   :maxdepth: 2
   :caption: Internals

   internals/architecture
   internals/dsp-signal-chain
   internals/configuration
   internals/sensor-framework
   internals/source-map

.. toctree::
   :maxdepth: 1
   :caption: Reference

   reference/config-json
   reference/http-routes
   reference/troubleshooting
   reference/limitations
   reference/credits
