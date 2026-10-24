#!/usr/bin/env python3
"""Media files for the AnkiNX test deck (tools/make_test_deck.sh).
Usage: test_deck_media.py <out dir>   (needs Pillow, numpy, soundfile)

Every file name starts with "ankinx_test_" (or "_ankinx_test_" for files
Anki must keep although no field names them: fonts, scripts, data)."""
import json
import math
import shutil
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from PIL import Image, ImageDraw, ImageFont

OUT = Path(sys.argv[1])
OUT.mkdir(parents=True, exist_ok=True)


def font(size):
    for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                 "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"):
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def labelled(width, height, background, label):
    img = Image.new("RGB", (width, height), background)
    d = ImageDraw.Draw(img)
    d.rectangle([4, 4, width - 5, height - 5], outline=(30, 30, 30), width=4)
    d.text((16, 12), label, fill=(20, 20, 20), font=font(max(14, min(height // 10, width // 22))))
    return img, d


# A map for Image Occlusion: four coloured regions where the masks go.
img, d = labelled(800, 500, (225, 236, 245), "")
d.rectangle([60, 60, 300, 200], fill=(205, 60, 60))           # rect mask (c1)
d.ellipse([440, 50, 680, 230], fill=(60, 90, 205))            # ellipse mask (c2)
d.polygon([(280, 460), (400, 270), (520, 460)], fill=(50, 160, 70))  # polygon mask (c3)
d.rectangle([600, 330, 760, 450], fill=(240, 180, 40))        # text mask (c4)
for (x, y), name in {(120, 115): "Red", (520, 125): "Blue", (365, 400): "Green", (630, 375): "Gold"}.items():
    d.text((x, y), name, fill=(255, 255, 255), font=font(30))
img.save(OUT / "ankinx_test_map.png")

# One image per format, each saying its format.
img, d = labelled(480, 270, (250, 220, 200), "JPEG")
for i in range(0, 480, 24):
    d.line([(i, 270), (480, 270 - i)], fill=(200, 120, 90), width=3)
img.save(OUT / "ankinx_test_photo.jpg", quality=85)
labelled(320, 180, (200, 240, 210), "PNG")[0].save(OUT / "ankinx_test_image.png")
labelled(320, 180, (230, 210, 250), "BMP")[0].save(OUT / "ankinx_test_bitmap.bmp")
labelled(320, 180, (250, 240, 180), "WebP")[0].save(OUT / "ankinx_test_badge.webp", "WEBP")
frames = []
for i in range(8):
    frame, fd = labelled(320, 180, (210, 230, 250), f"GIF frame {i + 1}/8")
    x = 40 + i * 32
    fd.ellipse([x, 90, x + 50, 140], fill=(220, 60, 60))
    frames.append(frame)
frames[0].save(OUT / "ankinx_test_anim.gif", save_all=True, append_images=frames[1:], duration=150, loop=0)
# Larger than the screen: should be scaled down to the card's width.
big, bd = labelled(2400, 1500, (240, 240, 240), "2400 x 1500 PNG (scaled down)")
for x in range(0, 2400, 100):
    bd.line([(x, 0), (x, 1500)], fill=(200, 200, 200), width=2)
for y in range(0, 1500, 100):
    bd.line([(0, y), (2400, y)], fill=(200, 200, 200), width=2)
big.save(OUT / "ankinx_test_large.png")
# SVG with <defs>/<use> (like Anki's LaTeX SVGs), a gradient and text.
(OUT / "ankinx_test_vector.svg").write_text("""<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="320" height="180" viewBox="0 0 320 180">
  <defs>
    <linearGradient id="g" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#4f8ef7"/><stop offset="1" stop-color="#a64ff7"/>
    </linearGradient>
    <circle id="dot" r="14"/>
  </defs>
  <rect x="2" y="2" width="316" height="176" rx="18" fill="url(#g)" stroke="#222" stroke-width="3"/>
  <use xlink:href="#dot" x="60" y="60" fill="#fff"/>
  <use xlink:href="#dot" x="110" y="60" fill="#ffd54f"/>
  <use xlink:href="#dot" x="160" y="60" fill="#81c784"/>
  <text x="30" y="140" font-family="sans-serif" font-size="34" fill="#fff">SVG</text>
</svg>
""")

# Tones: a different pitch per format, so you can tell which one played.
def tone(freqs, seconds=1.2, rate=44100):
    t = np.linspace(0, seconds, int(rate * seconds), endpoint=False)
    wave = sum(np.sin(2 * math.pi * f * t) for f in freqs) / len(freqs)
    fade = np.minimum(1, np.minimum(t, seconds - t) / 0.05)
    return (0.4 * wave * fade).astype(np.float32)

sf.write(OUT / "ankinx_test_a4.mp3", tone([440]), 44100, format="MP3")
sf.write(OUT / "ankinx_test_c5.ogg", tone([523.25]), 44100, format="OGG", subtype="VORBIS")
sf.write(OUT / "ankinx_test_e5.opus", tone([659.25], rate=48000), 48000, format="OGG", subtype="OPUS")
sf.write(OUT / "ankinx_test_g5.flac", tone([783.99]), 44100, format="FLAC")
sf.write(OUT / "ankinx_test_c6.wav", tone([1046.5]), 44100, format="WAV", subtype="PCM_16")
sf.write(OUT / "ankinx_test_chord.mp3", tone([261.63, 329.63, 392.0], seconds=2.0), 44100, format="MP3")

# Kept by Anki because the name starts with "_": a card font, a script and data.
for src in ("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"):
    if Path(src).exists():
        shutil.copy(src, OUT / "_ankinx_test_font.ttf")
        break
(OUT / "_ankinx_test_data.json").write_text(json.dumps({"capital": "Canberra", "loaded": True}))
(OUT / "_ankinx_test_lib.js").write_text(
    "// Loaded with <script src> from the media folder.\n"
    "window.ankinxTestLib = { greet: function (name) { return 'Hello, ' + name + '!'; } };\n")
print("\n".join(sorted(p.name for p in OUT.iterdir())))
