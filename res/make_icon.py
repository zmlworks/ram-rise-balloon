#!/usr/bin/env python3
"""Generate res/app.ico (memory chip with an up arrow). Build-time helper only (Pillow)."""
from PIL import Image, ImageDraw
import os
here = os.path.dirname(os.path.abspath(__file__))
S = 256
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
# chip body with pins on left/right
body = (52, 40, 204, 216)
pin = (176, 184, 192, 255)
for i in range(5):
    y = 58 + i * 32
    d.rounded_rectangle((22, y, 56, y + 16), radius=4, fill=pin)
    d.rounded_rectangle((200, y, 234, y + 16), radius=4, fill=pin)
d.rounded_rectangle(body, radius=22, fill=(40, 46, 56, 255), outline=(110, 122, 140, 255), width=8)
# up arrow (orange)
c = (255, 138, 61, 255)
d.polygon([(128, 60), (186, 128), (150, 128), (150, 196), (106, 196), (106, 128), (70, 128)], fill=c)
img.save(os.path.join(here, "app.ico"), sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (256, 256)])
img.save(os.path.join(here, "app_preview.png"))
