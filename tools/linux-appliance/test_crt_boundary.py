#!/usr/bin/env python3
"""Check the white integration fixture against the image's actual CRT warp."""
import sys
from PIL import Image

image = Image.open(sys.argv[1]).convert("RGB")
w, h = image.size
inside = outside = 0
for y in range(h):
    py = (y + 0.5) * 2 / h - 1
    for x in range(w):
        px = (x + 0.5) * 2 / w - 1
        u = (px * (1 + py * py / 64) + 1) / 2
        v = (py * (1 + px * px / 48) + 1) / 2
        distance = min(u * w, (1 - u) * w, v * h, (1 - v) * h)
        brightness = max(image.getpixel((x, y)))
        # Exclude only the antialiased transition, not a hand-picked crop.
        if distance < -2:
            assert brightness == 0, ("outside warp is lit", x, y, brightness)
            outside += 1
        elif distance > 2:
            assert brightness > 20, ("inside warp is black", x, y, brightness)
            inside += 1
assert outside > w + h and inside > w * h * 0.9
print(f"CRT boundary matches image warp: {inside} interior, {outside} exterior pixels")
