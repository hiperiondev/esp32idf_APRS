#!/usr/bin/env python3
"""Generate a real AFSK1200 (Bell 202) APRS WAV for validating the test harness."""
import math, struct, wave, sys

def crc16_x25(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc ^ 0xFFFF

def addr(call, last=False, repeated=False):
    if '-' in call:
        c, s = call.split('-'); s = int(s)
    else:
        c, s = call, 0
    c = c.ljust(6)
    out = bytearray((ord(ch) << 1) for ch in c)
    ssid = 0x60 | (s << 1) | (0x80 if repeated else 0) | (1 if last else 0)
    out.append(ssid)
    return bytes(out)

def build_frame(src, dst, path, info):
    addrs = [dst, src] + path
    f = bytearray()
    for i, a in enumerate(addrs):
        rep = a.endswith('*'); a = a.rstrip('*')
        f += addr(a, last=(i == len(addrs)-1), repeated=rep)
    f += bytes([0x03, 0xF0]) + info.encode('latin-1')
    crc = crc16_x25(f)
    f += bytes([crc & 0xFF, crc >> 8])
    return bytes(f)

def hdlc_bits(frame, preamble_flags=40, tail_flags=8):
    bits = []
    def flag():
        bits.extend([0,1,1,1,1,1,1,0])
    for _ in range(preamble_flags): flag()
    ones = 0
    for byte in frame:
        for i in range(8):
            bit = (byte >> i) & 1
            bits.append(bit)
            if bit:
                ones += 1
                if ones == 5:
                    bits.append(0); ones = 0
            else:
                ones = 0
    for _ in range(tail_flags): flag()
    return bits

def afsk(bits, rate=22050, amp=0.6):
    samples = []
    phase = 0.0
    tone = 1200
    spb = rate / 1200.0
    acc = 0.0
    for bit in bits:
        if bit == 0:
            tone = 2200 if tone == 1200 else 1200   # NRZI: 0 toggles
        acc += spb
        n = int(acc); acc -= n
        for _ in range(n):
            phase += 2*math.pi*tone/rate
            samples.append(amp*math.sin(phase))
    return samples

def write_wav(path, packets, rate=22050, gap_s=1.0):
    allsamp = [0.0]*int(rate*0.5)
    for (src, dst, pth, info) in packets:
        fr = build_frame(src, dst, pth, info)
        allsamp += afsk(hdlc_bits(fr), rate)
        allsamp += [0.0]*int(rate*gap_s)
    with wave.open(path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(b''.join(struct.pack('<h', int(max(-1,min(1,s))*32767)) for s in allsamp))

if __name__ == '__main__':
    pk = [
        ("N0CALL-9","APRS",["WIDE1-1","WIDE2-1"], "!4903.50N/07201.75W-Test one"),
        ("LU1ABC","APDW17",["WIDE1-1*"], "=3450.12S/05812.34W>Movil en ruta"),
        ("EA4XYZ-7","APRS",[], ":LU1ABC   :Hola que tal{12"),
    ]
    write_wav(sys.argv[1] if len(sys.argv)>1 else "sample1.wav", pk)
    print("ok")
