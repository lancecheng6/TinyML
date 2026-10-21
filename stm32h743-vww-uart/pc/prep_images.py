#!/usr/bin/env python3
"""prep_images.py — JPEG/PNG -> 160x120 RGB565 raw (+ expected int8 tensor)

Usage:
    python3 prep_images.py <input_dir_with_images> <output_dir>

The int8 tensor replicates the firmware math exactly (byte-for-byte):
    nearest-neighbour resize 160x120 -> 96x96
    RGB565 gray:  (77*R8 + 150*G8 + 29*B8) >> 8   (R8/G8/B8 expanded to 8 bit)
    int8 = clamp(gray - 128, -128, 127)
"""
import argparse
import glob
import os

from PIL import Image

W, H = 160, 120
IMG_W = IMG_H = 96


def rgb565_to_gray(p):
    r5 = (p >> 11) & 0x1F
    g6 = (p >> 5) & 0x3F
    b5 = p & 0x1F
    r8 = (r5 << 3) | (r5 >> 2)
    g8 = (g6 << 2) | (g6 >> 4)
    b8 = (b5 << 3) | (b5 >> 2)
    return (77 * r8 + 150 * g8 + 29 * b8) >> 8


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input_dir")
    ap.add_argument("output_dir")
    args = ap.parse_args()
    os.makedirs(args.output_dir, exist_ok=True)

    files = []
    for ext in ("*.jpg", "*.jpeg", "*.png", "*.bmp"):
        files += glob.glob(os.path.join(args.input_dir, ext))
    files.sort()
    print("found", len(files), "images")

    for path in files:
        name = os.path.splitext(os.path.basename(path))[0]
        im = Image.open(path).convert("RGB").resize((W, H), Image.BILINEAR)
        px = im.load()
        frame = []
        for y in range(H):
            for x in range(W):
                r, g, b = px[x, y]
                frame.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        with open(os.path.join(args.output_dir, name + ".rgb565"), "wb") as f:
            for p in frame:
                f.write(bytes((p & 0xFF, (p >> 8) & 0xFF)))

        tensor = bytearray(IMG_W * IMG_H)
        for y in range(IMG_H):
            sy = y * H // IMG_H
            for x in range(IMG_W):
                sx = x * W // IMG_W
                q = rgb565_to_gray(frame[sy * W + sx]) - 128
                tensor[y * IMG_W + x] = (max(-128, min(127, q))) & 0xFF
        with open(os.path.join(args.output_dir, name + ".i8"), "wb") as f:
            f.write(tensor)
        print("wrote", name)


if __name__ == "__main__":
    main()
