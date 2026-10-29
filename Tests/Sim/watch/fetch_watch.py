"""Download the UNA product photo and build the simulator's watch frame.

    python fetch_watch.py

Writes front.png (the photo) and watch_640.rgba (640 x 640 raw RGBA crop the
simulator's WatchHost loads). The photo is UNA Watch Ltd's, so it is not kept
in this repository. Without it the simulator shows the bare 240 x 240 display.
"""

import io
import os
import urllib.request

from PIL import Image

URL = ("https://unawatch.com/cdn/shop/files/"
       "CharcoleFront_74939a60-6d57-4b93-8724-6a16f210c66b.png?v=1788973528&width=1600")
HERE = os.path.dirname(os.path.abspath(__file__))
CX, CY = 803.5, 808.5            # display centre in the 1600 px photo

with urllib.request.urlopen(URL) as r:
    photo = Image.open(io.BytesIO(r.read())).convert("RGBA")
photo.save(os.path.join(HERE, "front.png"))

h = 500
crop = photo.crop((int(CX) - h, int(CY) - h - 20, int(CX) + h, int(CY) + h - 20))
bg = Image.new("RGBA", crop.size, (22, 22, 26, 255))
out = Image.alpha_composite(bg, crop).resize((640, 640), Image.LANCZOS)
with open(os.path.join(HERE, "watch_640.rgba"), "wb") as f:
    f.write(out.tobytes())
print("wrote front.png and watch_640.rgba")
