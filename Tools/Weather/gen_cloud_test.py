#!/usr/bin/env python3
"""
Phase A: Generate test cloud cover PNG for UDS verification.

Outputs a grayscale equirectangular-style cloud coverage map:
  - White (255) = full cloud (UDS Cloud Coverage = 10)
  - Black   (0) = clear sky    (UDS Cloud Coverage = 0)

Pattern is intentionally distinctive so the user can immediately
verify that UDS volumetric clouds form at the expected world XY
locations after C++ feeder writes this into UDS Cloud Coverage RT.

Stdlib only (zlib + struct).
"""

import argparse
import math
import os
import struct
import zlib


# ---------- Minimal PNG encoder (grayscale, 8-bit) ----------

def _png_chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def write_grayscale_png(path: str, pixels: list, width: int, height: int) -> None:
    """pixels: flat list of length width*height, each 0-255."""
    assert len(pixels) == width * height, "pixel count mismatch"

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)  # 8-bit grayscale

    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type: None
        row_start = y * width
        raw.extend(bytes(pixels[row_start : row_start + width]))
    idat = zlib.compress(bytes(raw), level=9)

    with open(path, "wb") as f:
        f.write(sig)
        f.write(_png_chunk(b"IHDR", ihdr))
        f.write(_png_chunk(b"IDAT", idat))
        f.write(_png_chunk(b"IEND", b""))


# ---------- Pattern generator ----------

def gen_test_pattern(size: int) -> list:
    """
    Distinctive recognizable pattern. After feeding into UDS, you should see:
      - Big soft cloud mass in CENTER     (gaussian blob, max 80%)
      - Hard dense blob in TOP-RIGHT      (small ~5% radius circle, full 100%)
      - Cloud BAND across BOTTOM 10%      (uniform 70%)
      - Small isolated blob BOTTOM-LEFT   (tiny dense circle)
      - Subtle CROSSHAIRS through middle  (orientation aid, ~15%)
      - Everything else: CLEAR SKY (0%)

    Coords (UE world XY in 200km area, UDS default):
      Image (0,0)         = top-left   = NW corner of the painted area
      Image (W,H)         = bottom-right = SE corner
    """
    W = H = size
    pixels = [0] * (W * H)

    cx, cy = W // 2, H // 2

    # Center gaussian cloud mass (soft)
    sigma = W / 6.0
    inv = 1.0 / (2.0 * sigma * sigma)
    for y in range(H):
        dy2 = (y - cy) * (y - cy)
        for x in range(W):
            d2 = dy2 + (x - cx) * (x - cx)
            v = math.exp(-d2 * inv) * 200.0  # 0..200
            i = y * W + x
            if v > pixels[i]:
                pixels[i] = int(v)

    # Hard dense blob in top-right
    bx, by = int(W * 0.78), int(H * 0.22)
    br = int(W * 0.05)
    br2 = br * br
    for y in range(max(0, by - br), min(H, by + br + 1)):
        dy2 = (y - by) * (y - by)
        for x in range(max(0, bx - br), min(W, bx + br + 1)):
            d2 = dy2 + (x - bx) * (x - bx)
            if d2 < br2:
                i = y * W + x
                if 255 > pixels[i]:
                    pixels[i] = 255

    # Bottom horizontal band
    band_top = int(H * 0.85)
    band_bot = int(H * 0.95)
    for y in range(band_top, band_bot):
        for x in range(W):
            i = y * W + x
            if 180 > pixels[i]:
                pixels[i] = 180

    # Small isolated blob bottom-left
    bx, by = int(W * 0.15), int(H * 0.70)
    br = int(W * 0.025)
    br2 = br * br
    for y in range(max(0, by - br), min(H, by + br + 1)):
        dy2 = (y - by) * (y - by)
        for x in range(max(0, bx - br), min(W, bx + br + 1)):
            d2 = dy2 + (x - bx) * (x - bx)
            if d2 < br2:
                i = y * W + x
                if 220 > pixels[i]:
                    pixels[i] = 220

    # Crosshairs (subtle orientation lines)
    for y in range(H):
        i = y * W + cx
        if 40 > pixels[i]:
            pixels[i] = 40
    for x in range(W):
        i = cy * W + x
        if 40 > pixels[i]:
            pixels[i] = 40

    return pixels


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate test cloud cover PNG for UDS Phase A.")
    parser.add_argument("--size", type=int, default=1024, help="PNG resolution (default 1024)")
    parser.add_argument(
        "--output",
        default=os.path.join(os.path.dirname(__file__), "output", "CloudSat_Test.png"),
        help="Output PNG path",
    )
    args = parser.parse_args()

    print(f"Generating {args.size}x{args.size} test cloud pattern...")
    pixels = gen_test_pattern(args.size)

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    write_grayscale_png(args.output, pixels, args.size, args.size)
    print(f"Wrote {args.output}")
    print()
    print("Next steps:")
    print("  1. Import the PNG into UE Content browser (drag & drop)")
    print("     -> creates Content/Weather/CloudSat_Test (UTexture2D)")
    print("  2. Set texture compression to 'UserInterface2D (RGBA)' or 'Grayscale'")
    print("  3. Drop ASatelliteCloudFeeder actor in level, assign CloudTexture")
    print("  4. PIE -> UDS volumetric clouds form per pattern in 200km area")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
