"""Interface-to-K1-plug schematics for the hardware chapter.

usage: k1_schem.py <outdir> <lang>
  -> k1_full_<lang>.png, k1_reduced_<lang>.png, k1_opto_<lang>.png
"""
import sys
import schemdraw
import schemdraw.elements as elm
schemdraw.use('matplotlib')
OUT = sys.argv[1]
LANG = sys.argv[2] if len(sys.argv) > 2 else 'en'
T = {
 'en': {'nc': 'n.c.', 'gnd': 'radio GND', 'multi': 'multiturn', 'np': 'non-polar', 'spk': 'speaker audio', 'flat': 'flat audio',
        'txa': 'TX audio (see the full interface)  ', 'rxa': 'RX audio (see the full interface)  '},
 'es': {'nc': 'n.c.', 'gnd': 'GND de la radio', 'multi': 'multivuelta', 'np': 'no polarizado', 'spk': 'audio de altavoz', 'flat': 'audio plano',
        'txa': 'audio TX (ver la interfaz completa)  ', 'rxa': 'audio RX (ver la interfaz completa)  '},
 'it': {'nc': 'n.c.', 'gnd': 'GND della radio', 'multi': 'multigiri', 'np': 'non polarizzato', 'spk': 'audio altoparlante', 'flat': 'audio piatto',
        'txa': 'audio TX (vedi interfaccia completa)  ', 'rxa': 'audio RX (vedi interfaccia completa)  '},
}[LANG]
BLUE = '#1d4ed8'; PLUG = '#7c2d12'; NOTE = '#555555'

def ptt_npn(d, x0, y0):
    d += elm.Label().at((x0-0.25, y0)).label('GPIO26 (PTT)', halign='right', color=BLUE)
    d += elm.Dot(open=True).at((x0, y0))
    d += elm.Resistor().right().at((x0, y0)).label('R9\n1k')
    d += elm.LED().right().label('D4\nLED')
    d += (nb := elm.Dot())
    d += elm.Resistor().down().at(nb.end).label('R10\n10k', loc='bottom'); d += elm.Ground()
    d += elm.Line().right(0.8).at(nb.end)
    d += (q := elm.BjtNpn(circle=True).anchor('base').label('Q1\n2N2222', loc='right'))
    d += elm.Ground().at(q.emitter)
    return q.collector

def build(kind):
    with schemdraw.Drawing(show=False) as d:
        d.config(fontsize=11, unit=2.0, lw=1.3)
        XP = 17.5  # plug block x (left pin column)
        # ---------------- TX path ----------------
        y_tx = 11.0
        d += elm.Label().at((-0.25, y_tx)).label('GPIO25 (DAC)', halign='right', color=BLUE)
        d += elm.Dot(open=True).at((0, y_tx))
        if kind == 'full':
            d += elm.Resistor().right().label('R1\n2k2')
            d += (n1 := elm.Dot())
            d += elm.Capacitor().down().at(n1.end).label('C2\n15n', loc='bottom'); d += elm.Ground()
            d += elm.Resistor().right().at(n1.end).label('R2\n2k2')
            d += (n2 := elm.Dot())
            d += elm.Capacitor().down().at(n2.end).label('C3\n15n', loc='bottom'); d += elm.Ground()
            d += elm.Capacitor(polar=True).right().at(n2.end).label('C1\n10µ')
            d += elm.Resistor().right().label('R3\n10k')
            d += elm.Line().right(1.0)
            d += (rv1 := elm.Potentiometer().down().label('RV1\n1k', loc='bottom'))
            d += elm.Ground()
            d += elm.Line().right().at(rv1.tap).tox(XP)
        else:  # reduced: trimmer divider from GPIO25 to GND, wiper -> cap -> MIC
            d += elm.Line().right(2.0)
            d += (rv1 := elm.Potentiometer().down().label('RV1\n10k\n'+T['multi'], loc='top'))
            d += elm.Ground()
            d += elm.Line().right(2.0).at(rv1.tap)
            d += elm.Capacitor().right().label('C1  ≥1 µF\n('+T['np']+')')
            d += elm.Line().right().tox(XP)
        tx_end = d.here
        # ---------------- RX path ----------------
        y_rx = 4.0
        d += elm.Label().at((-0.25, y_rx)).label('GPIO33 (ADC)', halign='right', color=BLUE)
        d += elm.Dot(open=True).at((0, y_rx))
        if kind == 'full':
            d += elm.Line().right(1.0)
            d += (n3 := elm.Dot())
            d += elm.Capacitor().down().at(n3.end).label('C5\n1n', loc='bottom'); d += elm.Ground()
            d += elm.Line().right(2.0).at(n3.end)
            d += (n4 := elm.Dot())
            d += elm.Schottky().up().at(n4.end).label('D1\nBAT85', loc='top')
            d += elm.Vdd().label('+3V3')
            d += elm.Schottky().down().at(n4.end).reverse().label('D2\nBAT85', loc='bottom'); d += elm.Ground()
            d += elm.Resistor().right().at(n4.end).label('R7\n1k')
            d += elm.Line().right(1.0)
            d += (n5 := elm.Dot())
            d += elm.Resistor().up().at(n5.end).label('R5\n10k', loc='bottom'); d += elm.Vdd().label('+3V3')
            d += elm.Resistor().down().at(n5.end).label('R6\n10k', loc='bottom'); d += elm.Ground()
            d += elm.Line().right(1.0).at(n5.end)
            d += elm.Capacitor(polar=True).right().label('C4\n10µ')
            d += elm.Line().right(1.0)
            rx_w = d.here
        else:
            d += elm.Line().right(3.0)
            d += elm.Capacitor().right().label('C2   10 nF ('+T['spk']+')\n1 µF ('+T['flat']+')')
            d += elm.Line().right(2.0)
            rx_w = d.here
        # trimmer across the speaker output: top = SPK, bottom = GND, wiper -> capacitor
        d += (rv2 := elm.Potentiometer().up().at((rx_w[0] + 1.0, rx_w[1] - 1.0)).label('RV2\n10k', loc='bottom'))
        d += elm.Line().at(rv2.tap).to(rx_w)
        d += elm.Ground().at(rv2.start)
        d += elm.Line().up(1.4).at(rv2.end)
        spk_from = d.here
        # ---------------- PTT ----------------
        y_ptt = -3.5
        col = ptt_npn(d, 0, y_ptt)
        # ---------------- plugs ----------------
        # 3.5 mm: MIC / PTT
        p35 = elm.Ic(pins=[elm.IcPin(name='TIP  (+V)', side='left', slot='3/3'),
                           elm.IcPin(name='RING  (MIC+)', side='left', slot='2/3'),
                           elm.IcPin(name='SLEEVE  (PTT)', side='left', slot='1/3')],
                     edgepadW=3.2, edgepadH=0.4, pinspacing=1.2, leadlen=0.6, lblsize=11, color=PLUG,
                     label='3.5 mm\nMIC / PTT', lblloc='top', lblofst=0.15)
        p25 = elm.Ic(pins=[elm.IcPin(name='TIP  (SPK+)', side='left', slot='3/3'),
                           elm.IcPin(name='RING', side='left', slot='2/3'),
                           elm.IcPin(name='SLEEVE  (GND)', side='left', slot='1/3')],
                     edgepadW=3.2, edgepadH=0.4, pinspacing=1.2, leadlen=0.6, lblsize=11, color=PLUG,
                     label='2.5 mm\nSPK', lblloc='top', lblofst=0.15)
        d += p35.at((XP, y_tx)).anchor('inL2')
        d += p25.at((XP, y_rx + 2.6)).anchor('inL1')
        # wiring to plugs
        d += elm.Line().at(spk_from).toy(p25.inL1[1])
        d += elm.Line().tox(p25.inL1[0])
        d += elm.Label().at(p25.inL2).label(T['nc'], halign='right', color=NOTE, fontsize=9)
        d += elm.Ground().at(p25.inL3)
        d += elm.Label().at((p25.inL3[0]+0.5, p25.inL3[1]-1.3)).label(T['gnd'], halign='left', color=NOTE, fontsize=9)
        d += elm.Label().at(p35.inL1).label(T['nc'], halign='right', color=NOTE, fontsize=9)
        # PTT: collector -> 3.5 SLEEVE ; emitter at GND (= 2.5 SLEEVE)
        sl = p35.inL3
        d += elm.Line().at(col).toy(col[1] + 0.8)
        d += elm.Line().tox(sl[0] - 1.5)
        d += elm.Line().toy(sl[1])
        d += elm.Line().tox(sl[0])
        # RING (MIC+) <- TX
        d += elm.Line().at(tx_end).toy(p35.inL2[1])
        d += elm.Line().tox(p35.inL2[0])
        d.save(f'{OUT}/k1_{kind}_{LANG}.png', dpi=130)


def build_opto():
    with schemdraw.Drawing(show=False) as d:
        d.config(fontsize=11, unit=2.0, lw=1.3)
        p35 = elm.Ic(pins=[elm.IcPin(name='TIP  (+V)', side='left', slot='3/3'),
                           elm.IcPin(name='RING  (MIC+)', side='left', slot='2/3'),
                           elm.IcPin(name='SLEEVE  (PTT)', side='left', slot='1/3')],
                     edgepadW=3.2, edgepadH=0.4, pinspacing=1.2, leadlen=0.6, lblsize=11, color=PLUG,
                     label='3.5 mm\nMIC / PTT', lblloc='top', lblofst=0.15).right().at((12, 6)).anchor('inL2')
        d += p35
        p25 = elm.Ic(pins=[elm.IcPin(name='TIP  (SPK+)', side='left', slot='3/3'),
                           elm.IcPin(name='RING', side='left', slot='2/3'),
                           elm.IcPin(name='SLEEVE  (GND)', side='left', slot='1/3')],
                     edgepadW=3.2, edgepadH=0.4, pinspacing=1.2, leadlen=0.6, lblsize=11, color=PLUG,
                     label='2.5 mm\nSPK', lblloc='top', lblofst=0.15).right().at((12, 0)).anchor('inL2')
        d += p25
        d += elm.Label().at(p35.inL1).label(T['nc'], halign='right', color=NOTE, fontsize=9)
        d += elm.Label().at(p35.inL2).label(T['txa'], halign='right', color=NOTE, fontsize=9)
        d += elm.Label().at(p25.inL1).label(T['rxa'], halign='right', color=NOTE, fontsize=9)
        d += elm.Label().at(p25.inL2).label(T['nc'], halign='right', color=NOTE, fontsize=9)
        o = elm.Optocoupler(box=True).right().at((3, 2.2)).anchor('anode').label('U1\nPC817', loc='bottom', ofst=0.3)
        d += o
        d += elm.Line().left(0.6).at(o.anode)
        d += elm.LED().left().reverse().label('D3\nLED', loc='top')
        d += elm.Resistor().up().label('R8\n220R', loc='top')
        d += elm.Vdd().label('+3V3')
        d += elm.Line().left(2.6).at(o.cathode)
        d += elm.Dot(open=True)
        d += elm.Label().at((d.here[0]-0.25, d.here[1])).label('GPIO26 (PTT)', halign='right', color=BLUE)
        d += elm.Line().right(1.0).at(o.collector)
        d += elm.Line().toy(p35.inL3[1])
        d += elm.Line().tox(p35.inL3[0])
        d += elm.Line().right(1.0).at(o.emitter)
        d += elm.Line().toy(p25.inL3[1])
        d += elm.Line().tox(p25.inL3[0])
        d += elm.Ground().at((p25.inL3[0]-0.6, p25.inL3[1]))
        d.save(f'{OUT}/k1_opto_{LANG}.png', dpi=130)

for k in ('full', 'reduced'):
    build(k)
build_opto()
