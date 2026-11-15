#!/usr/bin/env python3
"""Generate the host-build sources that are extracted from the firmware tree.

usage: gen_sources.py <project_root> <out_dir>

  sdkconfig.h  - every CONFIG_ symbol of the project's sdkconfig
  tz.c         - time_sync.c's timezone table and its three accessors
  dropnames.c  - igate.c's igate_drop_reason_name()
"""
import os
import re
import sys

root, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)

lines = ['#pragma once']
for line in open(os.path.join(root, 'sdkconfig')):
    m = re.match(r'(CONFIG_[A-Z0-9_]+)=(.*)', line.strip())
    if m:
        key, val = m.groups()
        lines.append(f'#define {key} {"1" if val == "y" else val}')
open(os.path.join(out, 'sdkconfig.h'), 'w').write('\n'.join(lines) + '\n')

src = open(os.path.join(root, 'main', 'time_sync.c')).read()
table = re.search(r'static const time_sync_tz_t TZ_TABLE\[\] = \{.*?\n\};', src, re.S).group(0)
funcs = re.search(r'#define TZ_TABLE_COUNT.*?(?=\nstatic void logUtcNow)', src, re.S).group(0)
open(os.path.join(out, 'tz.c'), 'w').write('#include "time_sync.h"\n#include <string.h>\n#include <time.h>\n\n' + table + '\n' + funcs + '\n')

src = open(os.path.join(root, 'components', 'igate', 'igate.c')).read()
func = re.search(r'const char \*igate_drop_reason_name\(drop_reason_t reason\) \{.*?\n\}', src, re.S).group(0)
open(os.path.join(out, 'dropnames.c'), 'w').write('#include "igate.h"\n\n' + func + '\n')
