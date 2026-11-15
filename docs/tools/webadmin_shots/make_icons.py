#!/usr/bin/env python3
"""Cut the aprs.fi symbol sprites into the <code>-<table>.png icons the web
admin requests from aprs.dprns.com, so the screenshots need no network.

usage: make_icons.py <aprs-symbols checkout> <out_dir>
"""
import os
import sys

from PIL import Image

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
for table, sheet in ((1, 0), (2, 1)):
    im = Image.open(os.path.join(src, 'png', f'aprs-symbols-64-{sheet}.png')).convert('RGBA')
    for code in range(33, 127):
        row, col = divmod(code - 33, 16)
        icon = im.crop((col * 64, row * 64, col * 64 + 64, row * 64 + 64)).resize((32, 32), Image.LANCZOS)
        icon.save(os.path.join(out, f'{code}-{table}.png'))
