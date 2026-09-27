# Host regression test — HDLC transmitter

🇬🇧 English · 🇪🇸 [Español](README.es.md) · 🇮🇹 [Italiano](README.it.md)

`test_ax25_tx_hdlc.c` compiles the real `src/ax25.c`, `src/crc_ccit.c` and the
FX.25 sources (`src/fx25.c`, `lwfec/rs.c`, `lwfec/gf.c`) for the host, against
the stand-in ESP-IDF headers in `stubs/`, under AddressSanitizer and
UndefinedBehaviorSanitizer.

It sends random AX.25 UI frames through `Ax25WriteTxFrame()` →
`Ax25TransmitCheck()` → `Ax25GetTxBit()` and checks every key-up:

- bit for bit against a reference HDLC encoder (frame + FCS stuffed as one
  field, including the stuffed 0 owed when a run of five 1s ends on the last
  FCS bit);
- through the in-tree receiver (`Ax25BitParse()` / `Ax25ReadNextRxFrame()`);
- through a strict receiver following Direwolf's `hdlc_rec.c` rules, which
  requires exactly seven data bits at the closing flag.

A second pass sends frames as FX.25 blocks through the in-tree receiver. While
`Ax25GetTxBit()` is clocked, the pages holding `Fx25ModeList` are unmapped with
`mprotect()`, standing in for the flash cache being disabled under the
cache-safe DAC ISR: a read of the table from the transmit bit path ends the run
with a segmentation fault.

A third pass builds frames with 0 to 8 digipeaters from TNC2 text through
`ax25_encode()` and `hdlcFrame()` and checks that only the last address carries
the end-of-address bit, that `ax25_decode()` recovers every digipeater and that
the frame survives a key-up and the in-tree receiver.

A fourth pass runs half duplex and measures how long each queued frame waits
before keying up: with `Ax25TimeSlot(0)` every frame keys up at once, even after
a non-zero time slot was set first; with a 2000 ms time slot every frame waits
at least 2000 ms.

A fifth pass checks the end of a key-up and the key-up gate: for the default
and several `Ax25TxTail()` lengths every frame must be followed by exactly one
closing flag plus the tail rounded up to whole flags, and a queued frame must not
key up while `getTransmit()` is true or the previous key-down's teardown is still
owed, then key up once both clear.

The report states how many frames ended their FCS on five 1s, so a PASS always
covers that case. The run needs a POSIX host (`mprotect()`, `sysconf()`).

```bash
make              # build and run, default seed
make SEED=0x2a    # another seed
make clean
```

Requires a host C compiler with ASan/UBSan (gcc or clang). Exit status is 0 on
PASS. Background: *What the frame encoder guarantees* in the "The DSP Signal
Chain" chapter of the documentation.
