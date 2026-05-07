#!/usr/bin/env python3
"""
Extract cloud density mask from NASA GIBS TrueColor PNG.

Algorithm: cloud_density = min(R, G, B) / 255
  - White/grey clouds  → min close to 255 → high density  ✓
  - Blue ocean         → min(R) ~20-40    → near-zero     ✓
  - Green/brown land   → min(B or R) low  → near-zero     ✓

Input:  RGBA or RGB equirectangular TrueColor PNG (e.g. from fetch_gibs_cloud.py)
Output: Grayscale 8-bit PNG, same resolution, linear cloud coverage 0-255.

Usage:
    python3 extract_cloud_mask.py
    python3 extract_cloud_mask.py --input output/CloudGlobal_TerraTrueColor_2026-05-07.png
    python3 extract_cloud_mask.py --input foo.png --output bar_mask.png --gamma 1.5

Stdlib only (zlib + struct).
"""

import argparse
import os
import struct
import zlib


# ---------------------------------------------------------------------------
# Minimal PNG decoder (8-bit RGB and RGBA only)
# ---------------------------------------------------------------------------

def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png_rgba(path: str):
    """
    Returns (width, height, pixels) where pixels is a flat list of
    (R, G, B, A) tuples, length == width * height.
    Only handles 8-bit RGB (color type 2) and RGBA (color type 6).
    """
    with open(path, "rb") as f:
        raw = f.read()

    assert raw[:8] == b"\x89PNG\r\n\x1a\n", "Not a PNG file"

    pos = 8
    ihdr = None
    idat_chunks = []

    while pos < len(raw):
        length = struct.unpack(">I", raw[pos : pos + 4])[0]
        tag = raw[pos + 4 : pos + 8]
        data = raw[pos + 8 : pos + 8 + length]
        pos += 12 + length  # skip CRC too

        if tag == b"IHDR":
            ihdr = data
        elif tag == b"IDAT":
            idat_chunks.append(data)
        elif tag == b"IEND":
            break

    assert ihdr is not None, "No IHDR chunk"
    width, height = struct.unpack(">II", ihdr[:8])
    bit_depth, color_type = struct.unpack("BB", ihdr[8:10])
    assert bit_depth == 8, f"Only 8-bit PNGs supported (got {bit_depth})"
    assert color_type in (2, 6), f"Only RGB (2) or RGBA (6) supported (got {color_type})"

    bpp = 4 if color_type == 6 else 3  # bytes per pixel
    stride = width * bpp               # bytes per row (without filter byte)

    compressed = b"".join(idat_chunks)
    raw_data = zlib.decompress(compressed)

    pixels = []
    prev_row = bytes(stride)

    for y in range(height):
        row_start = y * (stride + 1)
        ftype = raw_data[row_start]
        row = bytearray(raw_data[row_start + 1 : row_start + 1 + stride])

        # Reconstruct filtered scanline
        if ftype == 0:   # None
            pass
        elif ftype == 1: # Sub
            for i in range(bpp, stride):
                row[i] = (row[i] + row[i - bpp]) & 0xFF
        elif ftype == 2: # Up
            for i in range(stride):
                row[i] = (row[i] + prev_row[i]) & 0xFF
        elif ftype == 3: # Average
            for i in range(stride):
                a = row[i - bpp] if i >= bpp else 0
                b = prev_row[i]
                row[i] = (row[i] + (a + b) // 2) & 0xFF
        elif ftype == 4: # Paeth
            for i in range(stride):
                a = row[i - bpp] if i >= bpp else 0
                b = prev_row[i]
                c = prev_row[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + _paeth(a, b, c)) & 0xFF
        else:
            raise ValueError(f"Unknown PNG filter type {ftype} at row {y}")

        prev_row = bytes(row)

        for x in range(width):
            base = x * bpp
            r, g, b_ = row[base], row[base + 1], row[base + 2]
            a = row[base + 3] if bpp == 4 else 255
            pixels.append((r, g, b_, a))

    return width, height, pixels


# ---------------------------------------------------------------------------
# Minimal PNG encoder (grayscale 8-bit, same as gen_cloud_test.py)
# ---------------------------------------------------------------------------

def _png_chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def write_grayscale_png(path: str, pixels_gray: list, width: int, height: int) -> None:
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)

    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: None
        row_start = y * width
        raw.extend(bytes(pixels_gray[row_start : row_start + width]))

    with open(path, "wb") as f:
        f.write(sig)
        f.write(_png_chunk(b"IHDR", ihdr))
        f.write(_png_chunk(b"IDAT", zlib.compress(bytes(raw), level=6)))
        f.write(_png_chunk(b"IEND", b""))


# ---------------------------------------------------------------------------
# Cloud extraction
# ---------------------------------------------------------------------------

def extract_cloud_density(pixels: list, gamma: float = 1.0) -> list:
    """
    Convert RGBA pixel list to grayscale cloud density values 0-255.
    cloud_density = min(R, G, B) / 255   (min-channel approach)
    Optional gamma curve to sharpen the cloud edges.
    """
    result = []
    for r, g, b, _a in pixels:
        density = min(r, g, b) / 255.0
        if gamma != 1.0:
            density = density ** gamma
        result.append(int(density * 255 + 0.5))
    return result


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def _default_input() -> str:
    out_dir = os.path.join(os.path.dirname(__file__), "output")
    # Pick the most recent TrueColor file
    candidates = sorted(
        [f for f in os.listdir(out_dir) if "TrueColor" in f and f.endswith(".png")],
        reverse=True,
    )
    if candidates:
        return os.path.join(out_dir, candidates[0])
    raise FileNotFoundError(
        "No TrueColor PNG found in Tools/Weather/output/. "
        "Run fetch_gibs_cloud.py first."
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extract cloud density mask from NASA GIBS TrueColor PNG."
    )
    parser.add_argument("--input", default=None,
                        help="Input TrueColor PNG (default: latest in output/)")
    parser.add_argument("--output", default=None,
                        help="Output grayscale PNG (default: <input>_CloudMask.png)")
    parser.add_argument("--gamma", type=float, default=1.0,
                        help="Gamma curve applied to cloud density (>1 = sharper edges, "
                             "default 1.0 = linear)")
    args = parser.parse_args()

    if args.input is None:
        args.input = _default_input()

    if args.output is None:
        base = os.path.splitext(args.input)[0]
        args.output = base + "_CloudMask.png"

    print(f"Input:  {args.input}")
    print(f"Output: {args.output}")
    print(f"Gamma:  {args.gamma}")

    print("Decoding PNG...")
    width, height, pixels = read_png_rgba(args.input)
    print(f"  {width}x{height} pixels ({len(pixels)} total)")

    print("Extracting cloud density (min-channel)...")
    gray = extract_cloud_density(pixels, gamma=args.gamma)

    print("Writing grayscale PNG...")
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    write_grayscale_png(args.output, gray, width, height)
    size_mb = os.path.getsize(args.output) / 1024 / 1024
    print(f"Wrote {args.output} ({size_mb:.1f} MB)")
    print()
    print("UE import settings for the cloud mask:")
    print("  Compression Settings: Grayscale (or Masks/no sRGB)")
    print("  sRGB: OFF  (linear density values)")
    print("  Tiling X: Wrap  (longitude is cyclic)")
    print("  Tiling Y: Clamp (poles are not cyclic)")
    print("  Generate Mip Maps: ON")
    print()
    print("Usage:")
    print("  Macro Shell material  → Opacity = CloudMask sample × MacroAlpha (from MPC)")
    print("  SatelliteCloudFeeder  → assign as GlobalCloudTexture")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
