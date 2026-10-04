#!/usr/bin/env python3
"""Convert a generated 2x2 sheet to four fixed-size RGB565 theme assets."""
from pathlib import Path
from PIL import Image
import argparse,struct
p=argparse.ArgumentParser();p.add_argument('sheet',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
im=Image.open(a.sheet).convert('RGB');w,h=im.size
for name,(x,y) in zip(['welcome-01','welcome-02','farewell-01','farewell-02'],[(0,0),(1,0),(0,1),(1,1)]):
 scene=im.crop((x*w//2,y*h//2,(x+1)*w//2,(y+1)*h//2)).resize((240,240),Image.Resampling.LANCZOS)
 scene.save(a.output/(name+'.png'))
 (a.output/(name+'.rgb565')).write_bytes(b''.join(struct.pack('>H',((r>>3)<<11)|((g>>2)<<5)|(b>>3)) for r,g,b in scene.getdata()))
