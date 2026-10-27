#!/usr/bin/env python3
"""Repack the indexed 64x704 cloud cell PNG into the runtime 4bpp cloud data."""
from pathlib import Path
from PIL import Image
import argparse,struct
p=argparse.ArgumentParser(description=__doc__);p.add_argument('png',type=Path);p.add_argument('output_directory',type=Path);a=p.parse_args()
im=Image.open(a.png)
if im.mode!='P' or im.size!=(64,704):raise SystemExit('Expected indexed 64x704 PNG; keep the eleven cells in their existing order.')
if max(im.getdata())>15:raise SystemExit('Only indices 0..15 are supported.')
b=bytearray()
for ty in range(0,704,8):
 for tx in range(0,64,8):
  for y in range(8):
   for x in range(0,8,2):b.append(im.getpixel((tx+x,ty+y))|(im.getpixel((tx+x+1,ty+y))<<4))
a.output_directory.mkdir(parents=True,exist_ok=True)
(a.output_directory/'foreground.bin').write_bytes(b)
pal=im.getpalette()[:48];v=[(pal[i]>>3)|((pal[i+1]>>3)<<5)|((pal[i+2]>>3)<<10) for i in range(0,48,3)]
(a.output_directory/'foreground_palette.bin').write_bytes(struct.pack('<16H',*v))
print('Wrote',len(b),'bytes and 16 colors. Preserve silhouette shade slots 4..12.')
