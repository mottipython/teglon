#!/usr/bin/env python3
"""Turn the drawings in assets/src (flat green or magenta backgrounds) into the
pictures the Lazy Peregrin screensaver builds in, under assets/art.

    tools/make_sprites.py

Objects are cut out of their background and saved as PNG with transparency; the
sea and the land are full pictures and are saved as JPEG. Needs ImageMagick and numpy.
"""
import subprocess
import numpy as np

SRC, OUT = "assets/src", "assets/art"
# name: (background colour to cut out, size in pixels along the given side)
OBJECTS = {
    "walk1": ("green", ("h", 340)), "walk2": ("green", ("h", 354)), "sleep": ("green", None), "wake": ("green", None),
    "math": ("magenta", ("h", 330)), "home": ("magenta", ("h", 420)), "flag": ("magenta", ("h", 130)),
    "tree1": ("magenta", ("h", 170)), "tree2": ("magenta", ("h", 190)),
    "mountain": ("magenta", ("w", 330)), "hill": ("magenta", ("w", 260)),
    "compass": ("magenta", ("h", 440)), "banner": ("magenta", ("w", 900)),
}
BACKGROUNDS = {"sea": "2752x1536", "land": "1024x1024"}

def load(path):
    w, h = map(int, subprocess.check_output(["magick", "identify", "-format", "%w %h", path]).split())
    raw = subprocess.check_output(["magick", path, "-depth", "8", "RGB:-"])
    return np.frombuffer(raw, np.uint8).reshape(h, w, 3).astype(np.float32)

def key(img, colour):
    """Flat background to alpha, and take its colour cast off the edges."""
    r, g, b = img[..., 0].copy(), img[..., 1].copy(), img[..., 2].copy()
    if colour == "green":
        spill = g - np.maximum(r, b)
        g = np.where(spill > 0, np.maximum(r, b) + np.minimum(spill, 6), g)
    else:
        spill = np.minimum(r, b) - g
        cut = np.clip(spill, 0, None) * 0.92
        r, b = r - cut, b - cut
    alpha = np.clip(1.0 - (spill - 24) / 70.0, 0, 1)
    return np.dstack([r, g, b, alpha * 255]).clip(0, 255).astype(np.uint8)

def crop(a):
    """Trim to the drawing, ignoring stray specks left in the background."""
    solid = a[..., 3] > 128
    rows = np.where(solid.sum(axis=1) > 3)[0]
    cols = np.where(solid.sum(axis=0) > 3)[0]
    return a[rows.min():rows.max() + 1, cols.min():cols.max() + 1]

def save(rgba, w, h, path):
    subprocess.run(["magick", "-size", f"{rgba.shape[1]}x{rgba.shape[0]}", "-depth", "8", "RGBA:-",
                    "-filter", "Lanczos", "-resize", f"{w}x{h}!", "-define", "png:compression-level=9", path],
                   input=rgba.tobytes(), check=True)

def main():
    subprocess.run(["mkdir", "-p", OUT], check=True)
    cut = {n: crop(key(load(f"{SRC}/{n}.jpeg"), c)) for n, (c, _) in OBJECTS.items()}
    fraa_scale = 340 / cut["walk1"].shape[0]  # the sleeping and waking fraa keep the walking one's scale
    for n, (_, size) in OBJECTS.items():
        a = cut[n]
        if size is None: f = fraa_scale
        else: f = size[1] / (a.shape[0] if size[0] == "h" else a.shape[1])
        w, h = max(1, round(a.shape[1] * f)), max(1, round(a.shape[0] * f))
        save(a, w, h, f"{OUT}/{n}.png")
        print(n, w, h)
    for n, size in BACKGROUNDS.items():
        subprocess.run(["magick", f"{SRC}/{n}.jpeg", "-resize", size + "!", "-quality", "88", f"{OUT}/{n}.jpg"], check=True)
        print(n, size)

if __name__ == "__main__":
    main()
