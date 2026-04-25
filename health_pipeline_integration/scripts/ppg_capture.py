#!/usr/bin/env python3
"""
Usage: python3 ppg_capture.py uart_logs/ppg_raw.txt
Strips [ELF] prefix, keeps only red,ir lines, saves to uart_logs/ppg_clean.txt
"""
import sys, os, re
from datetime import datetime

infile = sys.argv[1]
os.makedirs("uart_logs", exist_ok=True)
outfile = os.path.join("uart_logs", f"ppg_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt")

samples = []
with open(infile) as f:
    for line in f:
        # strip [ELF] prefix if present
        line = re.sub(r'^\[ELF\]\s*', '', line).strip()
        parts = line.split(',')
        if len(parts) == 2:
            try:
                int(parts[0]); int(parts[1])
                samples.append(line)
            except ValueError:
                pass

with open(outfile, 'w') as f:
    f.write('\n'.join(samples) + '\n')

print(f"Saved {len(samples)} samples → {outfile}")