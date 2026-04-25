#!/usr/bin/env python3
"""
Usage: python3 ecg_capture.py uart_logs/ecg_raw.txt
"""
import sys, os, re
from datetime import datetime

infile = sys.argv[1]
os.makedirs("uart_logs", exist_ok=True)
outfile = os.path.join("uart_logs", f"ecg_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt")

samples = []
with open(infile) as f:
    for line in f:
        line = re.sub(r'^\[ELF\]\s*', '', line).strip()
        try:
            float(line)
            samples.append(line)
        except ValueError:
            pass

with open(outfile, 'w') as f:
    f.write('\n'.join(samples) + '\n')

print(f"Saved {len(samples)} samples → {outfile}")