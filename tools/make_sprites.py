#!/usr/bin/env python3
"""Turn the fraa drawings (flat green background) into the sprite file the
screensaver embeds.

    tools/make_sprites.py DIR_WITH_walk1/walk2/sleep/wake.jpeg

Writes assets/fraa-*.png (for looking at) and assets/fraa.rgba: for each of the
four sprites a little-endian uint16 width and height, then width*height RGBA bytes.
Needs ImageMagick and numpy.
"""
import struct, subprocess, sys
import numpy as np

NAMES = ["walk1", "walk2", "sleep", "wake"]
WALK_HEIGHT = 340  # pixels tall the walking fraa is stored at; the rest keep the same scale

def load(path):
    w, h = map(int, subprocess.check_output(["magick", "identify", "-format", "%w %h", path]).split())
    raw = subprocess.check_output(["magick", path, "-depth", "8", "RGB:-"])
    return np.frombuffer(raw, np.uint8).reshape(h, w, 3).astype(np.float32)

def key(img):
    """Green screen to alpha, and take the green cast off the edges."""
    r, g, b = img[..., 0], img[..., 1], img[..., 2]
    spill = g - np.maximum(r, b)              # how much greener than anything else
    alpha = np.clip(1.0 - (spill - 24) / 70.0, 0, 1)
    g2 = np.where(spill > 0, np.maximum(r, b) + np.minimum(spill, 6), g)
    out = np.dstack([r, g2, b, alpha * 255]).clip(0, 255).astype(np.uint8)
    return out

def bbox(a):
    ys, xs = np.where(a[..., 3] > 40)
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1

def resize(rgba, w, h):
    src = subprocess.run(["magick", "-size", f"{rgba.shape[1]}x{rgba.shape[0]}", "-depth", "8", "RGBA:-",
                          "-filter", "Lanczos", "-resize", f"{w}x{h}!", "-depth", "8", "RGBA:-"],
                         input=rgba.tobytes(), capture_output=True, check=True).stdout
    return np.frombuffer(src, np.uint8).reshape(h, w, 4)

def main(src):
    cut = {}
    for n in NAMES:
        a = key(load(f"{src}/{n}.jpeg"))
        x0, y0, x1, y1 = bbox(a)
        cut[n] = a[y0:y1, x0:x1]
    scale = WALK_HEIGHT / cut["walk1"].shape[0]
    blob = b""
    for n in NAMES:
        a = cut[n]
        # the two walking frames are made the same height so the toddle does not pump
        f = WALK_HEIGHT / a.shape[0] if n.startswith("walk") else scale
        w, h = max(1, round(a.shape[1] * f)), max(1, round(a.shape[0] * f))
        s = resize(a, w, h)
        subprocess.run(["magick", "-size", f"{w}x{h}", "-depth", "8", "RGBA:-", f"assets/fraa-{n}.png"], input=s.tobytes(), check=True)
        blob += struct.pack("<HH", w, h) + s.tobytes()
        print(n, w, h)
    open("assets/fraa.rgba", "wb").write(blob)
    print("assets/fraa.rgba", len(blob), "bytes")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
