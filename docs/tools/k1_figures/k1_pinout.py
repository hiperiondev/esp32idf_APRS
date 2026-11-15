"""Kenwood K1 two-plug pinout drawing.

usage: k1_pinout.py <outdir> <lang>  -> k1_pinout_<lang>.png
"""
import sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle, Polygon
OUT, LANG = sys.argv[1], sys.argv[2]
T = {
 'en': dict(p35='3.5 mm plug — MIC / PTT', p25='2.5 mm plug — SPEAKER',
            t35='+V from radio\n(mic bias) — leave open', r35='MIC+ (audio in)\n← TX audio', s35='MIC− / PTT\n← PTT switch',
            t25='SPK+ (audio out)\n→ RX audio', r25='not used\n(data on some models)', s25='GND (radio ground)\n= interface GND',
            ptt='Transmit = short 3.5 mm SLEEVE to 2.5 mm SLEEVE', warn='Never tie the 3.5 mm SLEEVE to GND: the radio would key up permanently.',
            tip='TIP', ring='RING', sleeve='SLEEVE'),
 'es': dict(p35='Plug 3,5 mm — MIC / PTT', p25='Plug 2,5 mm — ALTAVOZ',
            t35='+V de la radio\n(polarización mic) — sin conectar', r35='MIC+ (entrada de audio)\n← audio TX', s35='MIC− / PTT\n← llave de PTT',
            t25='SPK+ (salida de audio)\n→ audio RX', r25='sin uso\n(datos en algunos modelos)', s25='GND (masa de la radio)\n= GND de la interfaz',
            ptt='Transmitir = unir SLEEVE de 3,5 mm con SLEEVE de 2,5 mm', warn='Nunca conectes el SLEEVE de 3,5 mm a GND: la radio quedaría transmitiendo.',
            tip='TIP', ring='RING', sleeve='SLEEVE'),
 'it': dict(p35='Spina 3,5 mm — MIC / PTT', p25='Spina 2,5 mm — ALTOPARLANTE',
            t35='+V dalla radio\n(polarizzazione mic) — non collegare', r35='MIC+ (ingresso audio)\n← audio TX', s35='MIC− / PTT\n← interruttore PTT',
            t25='SPK+ (uscita audio)\n→ audio RX', r25='non usato\n(dati su alcuni modelli)', s25='GND (massa della radio)\n= GND interfaccia',
            ptt='Trasmissione = collegare SLEEVE 3,5 mm a SLEEVE 2,5 mm', warn='Non collegare mai lo SLEEVE da 3,5 mm a GND: la radio resterebbe in trasmissione.',
            tip='TIP', ring='RING', sleeve='SLEEVE'),
}[LANG]
COL = {'tip': '#d4a017', 'ring': '#c0c0c0', 'sleeve': '#9e9e9e', 'ins': '#222222', 'body': '#333333'}

def plug(ax, y, scale, title, labels):
    h = 1.0 * scale
    x = 0.6
    # tip (pointed)
    segs = [('tip', 1.3), ('ins', 0.25), ('ring', 1.6), ('ins', 0.25), ('sleeve', 2.6)]
    xs = []
    ax.add_patch(Polygon([[x, y], [x + 0.45, y + h / 2], [x + 1.3, y + h / 2], [x + 1.3, y - h / 2], [x + 0.45, y - h / 2]],
                         closed=True, fc=COL['tip'], ec='k', lw=1))
    xs.append((x + 0.7, 'tip')); x += 1.3
    for kind, w in segs[1:]:
        ax.add_patch(Rectangle((x, y - h / 2), w, h, fc=COL[kind], ec='k', lw=1))
        if kind != 'ins':
            xs.append((x + w / 2, kind))
        x += w
    ax.add_patch(FancyBboxPatch((x, y - h * 0.9), 4.2, h * 1.8, boxstyle='round,pad=0.05,rounding_size=0.25', fc=COL['body'], ec='k'))
    ax.text(x + 2.1, y, title, color='white', ha='center', va='center', fontsize=10, fontweight='bold')
    for (cx, kind), (txt) in zip(xs, labels):
        drop = 2.35 if kind == 'tip' else 0.6
        ax.plot([cx, cx], [y - h / 2 - 0.05, y - h / 2 - drop], color='#444', lw=1)
        ax.text(cx, y - h / 2 - drop - 0.1, T[kind].upper(), ha='center', va='top', fontsize=9.5, fontweight='bold')
        ax.text(cx, y - h / 2 - drop - 0.55, txt, ha='center', va='top', fontsize=8.5, color='#333')
    return xs

fig, ax = plt.subplots(figsize=(10.5, 8.2), dpi=120)
ax.set_xlim(-0.8, 12.0); ax.set_ylim(-9.6, 3.0); ax.axis('off')
plug(ax, 2.0, 1.0, T['p35'], [T['t35'], T['r35'], T['s35']])
plug(ax, -3.3, 0.78, T['p25'], [T['t25'], T['r25'], T['s25']])
ax.text(0.0, -8.55, '⚡ ' + T['ptt'], fontsize=10.5, fontweight='bold', color='#9b1c1c')
ax.text(0.0, -9.15, T['warn'], fontsize=9.5, color='#9b1c1c')
plt.savefig(f'{OUT}/k1_pinout_{LANG}.png', bbox_inches='tight', facecolor='white')
