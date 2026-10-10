#!/usr/bin/env python3
"""Convert the simulator's *_home.bmp screenshots to review/home/*.png.

    SDL_VIDEODRIVER=dummy ./build/openams-ui-sim --screenshots review/all
    python3 tools/bmp2png.py            # review/all/*_home.bmp -> review/home/*.png
"""
import glob
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "review", "all")
DST = os.path.join(ROOT, "review", "home")

os.makedirs(DST, exist_ok=True)
for old in glob.glob(os.path.join(DST, "*.png")):
    os.remove(old)
for path in sorted(glob.glob(os.path.join(SRC, "*_home.bmp"))):
    name = os.path.splitext(os.path.basename(path))[0] + ".png"
    Image.open(path).convert("RGB").save(os.path.join(DST, name))
print("wrote", len(glob.glob(os.path.join(DST, "*.png"))), "PNGs to review/home")
