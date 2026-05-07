#!/usr/bin/env python3
"""
Fetch global cloud cover image from NASA GIBS WMS endpoint.

Output: equirectangular PNG (lon -180..180, lat -90..90), full globe.
Usage:
    python3 fetch_gibs_cloud.py                    # Terra+Aqua 3-day composite (recommended)
    python3 fetch_gibs_cloud.py --days 5           # more days, fewer gaps
    python3 fetch_gibs_cloud.py --sources TerraTrueColor   # single source
    python3 fetch_gibs_cloud.py --date 2024-04-28
    python3 fetch_gibs_cloud.py --width 8192 --height 4096

Default behavior:
    Fetches MODIS Terra + MODIS Aqua for the last 3 days (6 frames total),
    composites them with per-pixel max to fill orbital swath gaps, then
    runs a scanline gap-fill pass for any remaining black pixels.
    Automatically generates a grayscale CloudMask PNG in the same directory.

Available sources (--sources, comma-separated):
    TerraTrueColor   MODIS Terra ~10:30 local, RGB true color
    AquaTrueColor    MODIS Aqua  ~13:30 local, RGB true color (3h offset fills more gaps)
    VIIRS            VIIRS/SNPP  higher resolution true color
    CloudFraction    MODIS Terra cloud fraction (palette-encoded, scientific use)

Stdlib only (urllib + zlib + struct).
"""

import argparse
import datetime
import os
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import zlib


GIBS_WMS = "https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi"

LAYERS = {
    "TerraTrueColor":  "MODIS_Terra_CorrectedReflectance_TrueColor",
    "AquaTrueColor":   "MODIS_Aqua_CorrectedReflectance_TrueColor",
    "VIIRS":           "VIIRS_SNPP_CorrectedReflectance_TrueColor",
    "CloudFraction":   "MODIS_Terra_Cloud_Fraction_Day",
}


# ---------------------------------------------------------------------------
# Network helpers
# ---------------------------------------------------------------------------

def build_url(layer_id: str, width: int, height: int, date_str: str) -> str:
    params = {
        "SERVICE": "WMS",
        "REQUEST": "GetMap",
        "VERSION": "1.1.1",
        "LAYERS": layer_id,
        "STYLES": "",
        "FORMAT": "image/png",
        "SRS": "EPSG:4326",
        "WIDTH": str(width),
        "HEIGHT": str(height),
        "BBOX": "-180,-90,180,90",
        "TIME": date_str,
    }
    return GIBS_WMS + "?" + urllib.parse.urlencode(params)


def fetch_bytes(url: str, timeout: float = 90.0) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "EagleCloud/0.1"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        if resp.status != 200:
            raise RuntimeError(f"HTTP {resp.status}")
        data = resp.read()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        snippet = data[:500].decode("utf-8", errors="replace")
        raise RuntimeError(f"Response not PNG:\n{snippet}")
    return data


def find_recent_date(layer_id: str, width: int, height: int, max_days_back: int = 5) -> str:
    today = datetime.datetime.now(datetime.timezone.utc).date()
    for offset in range(max_days_back + 1):
        d = today - datetime.timedelta(days=offset)
        ds = d.isoformat()
        url = build_url(layer_id, width, height, ds)
        try:
            req = urllib.request.Request(url, method="HEAD",
                                         headers={"User-Agent": "EagleCloud/0.1"})
            with urllib.request.urlopen(req, timeout=15) as r:
                if r.status == 200:
                    return ds
        except (urllib.error.URLError, urllib.error.HTTPError, RuntimeError):
            pass
        time.sleep(0.2)
    return today.isoformat()


# ---------------------------------------------------------------------------
# Minimal PNG codec (8-bit RGB / RGBA)
# ---------------------------------------------------------------------------

def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def decode_png(data: bytes):
    """Return (width, height, flat_rgba_bytearray) from in-memory PNG bytes."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos = 8
    ihdr = None
    idat_parts = []
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos+4])[0]
        tag    = data[pos+4:pos+8]
        chunk  = data[pos+8:pos+8+length]
        pos   += 12 + length
        if tag == b"IHDR":
            ihdr = chunk
        elif tag == b"IDAT":
            idat_parts.append(chunk)
        elif tag == b"IEND":
            break

    w, h          = struct.unpack(">II", ihdr[:8])
    bit_depth, ct = struct.unpack("BB",  ihdr[8:10])
    assert bit_depth == 8 and ct in (2, 6), f"Only 8-bit RGB/RGBA (got bd={bit_depth} ct={ct})"

    bpp    = 4 if ct == 6 else 3
    stride = w * bpp
    raw    = zlib.decompress(b"".join(idat_parts))

    out      = bytearray(w * h * 4)
    prev_row = bytes(stride)
    for y in range(h):
        rs    = y * (stride + 1)
        ftype = raw[rs]
        row   = bytearray(raw[rs+1:rs+1+stride])
        if ftype == 1:
            for i in range(bpp, stride):
                row[i] = (row[i] + row[i-bpp]) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                row[i] = (row[i] + prev_row[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                a = row[i-bpp] if i >= bpp else 0
                row[i] = (row[i] + (a + prev_row[i]) // 2) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                a = row[i-bpp]    if i >= bpp else 0
                b = prev_row[i]
                c = prev_row[i-bpp] if i >= bpp else 0
                row[i] = (row[i] + _paeth(a, b, c)) & 0xFF
        prev_row = bytes(row)
        base_out = y * w * 4
        for x in range(w):
            px = x * bpp
            out[base_out+x*4]   = row[px]
            out[base_out+x*4+1] = row[px+1]
            out[base_out+x*4+2] = row[px+2]
            out[base_out+x*4+3] = row[px+3] if bpp == 4 else 255
    return w, h, out


def _png_chunk(tag: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag+data) & 0xFFFFFFFF)


def encode_png_rgba(w: int, h: int, rgba: bytearray) -> bytes:
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(rgba[y*w*4:(y+1)*w*4])
    return (b"\x89PNG\r\n\x1a\n"
            + _png_chunk(b"IHDR", ihdr)
            + _png_chunk(b"IDAT", zlib.compress(bytes(raw), level=6))
            + _png_chunk(b"IEND", b""))


def encode_png_gray(w: int, h: int, gray: bytearray) -> bytes:
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(gray[y*w:(y+1)*w])
    return (b"\x89PNG\r\n\x1a\n"
            + _png_chunk(b"IHDR", ihdr)
            + _png_chunk(b"IDAT", zlib.compress(bytes(raw), level=6))
            + _png_chunk(b"IEND", b""))


def make_cloud_mask(rgba: bytearray) -> bytearray:
    """min(R,G,B) per pixel → grayscale cloud density (white=cloud, black=clear/land/ocean)."""
    n = len(rgba) // 4
    gray = bytearray(n)
    for i in range(n):
        b = i * 4
        gray[i] = min(rgba[b], rgba[b+1], rgba[b+2])
    return gray


# ---------------------------------------------------------------------------
# Multi-day max composite
# ---------------------------------------------------------------------------

def composite_max(frames: list) -> tuple:
    """
    frames: list of (w, h, rgba_bytearray)
    Returns (w, h, rgba_bytearray) where each channel is the per-pixel max
    across all frames. Black pixels (R=G=B=0) are treated as no-data and
    skipped so gap areas fill from adjacent days.
    """
    w, h, base = frames[0]
    result = bytearray(base)  # start with first frame

    for _, _, frame in frames[1:]:
        for i in range(0, w * h * 4, 4):
            r1, g1, b1 = result[i], result[i+1], result[i+2]
            r2, g2, b2 = frame[i],  frame[i+1],  frame[i+2]
            # If current pixel is no-data (black), replace unconditionally
            if r1 == 0 and g1 == 0 and b1 == 0:
                result[i]   = r2
                result[i+1] = g2
                result[i+2] = b2
                result[i+3] = frame[i+3]
            # Otherwise take per-channel max (brightens / fills partial gaps)
            elif not (r2 == 0 and g2 == 0 and b2 == 0):
                result[i]   = max(r1, r2)
                result[i+1] = max(g1, g2)
                result[i+2] = max(b1, b2)

    return w, h, result


def gap_fill(rgba: bytearray, w: int, h: int) -> None:
    """
    Fill remaining black (no-data) pixels by scanline color propagation.
    4 directional passes (L→R, R→L, T→B, B→T), repeated twice.
    Each black pixel takes the color of its nearest non-black neighbor.
    Pure Python, no external deps. ~5-15s for 4096×2048.
    """
    def is_black(i: int) -> bool:
        return rgba[i] < 4 and rgba[i+1] < 4 and rgba[i+2] < 4

    for _ in range(2):
        # Left → Right
        for y in range(h):
            base = y * w * 4
            for x in range(1, w):
                i = base + x * 4
                if is_black(i):
                    j = i - 4
                    rgba[i], rgba[i+1], rgba[i+2] = rgba[j], rgba[j+1], rgba[j+2]
        # Right → Left
        for y in range(h):
            base = y * w * 4
            for x in range(w - 2, -1, -1):
                i = base + x * 4
                if is_black(i):
                    j = i + 4
                    rgba[i], rgba[i+1], rgba[i+2] = rgba[j], rgba[j+1], rgba[j+2]
        # Top → Bottom
        for x in range(w):
            for y in range(1, h):
                i = (y * w + x) * 4
                if is_black(i):
                    j = ((y - 1) * w + x) * 4
                    rgba[i], rgba[i+1], rgba[i+2] = rgba[j], rgba[j+1], rgba[j+2]
        # Bottom → Top
        for x in range(w):
            for y in range(h - 2, -1, -1):
                i = (y * w + x) * 4
                if is_black(i):
                    j = ((y + 1) * w + x) * 4
                    rgba[i], rgba[i+1], rgba[i+2] = rgba[j], rgba[j+1], rgba[j+2]


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description="Download global cloud cover from NASA GIBS.")
    parser.add_argument("--sources", default="TerraTrueColor,AquaTrueColor",
                        help="Comma-separated layer names to composite "
                             "(default: TerraTrueColor,AquaTrueColor).")
    parser.add_argument("--date",  default=None,
                        help="Most recent date YYYY-MM-DD (UTC). Default: auto-detect.")
    parser.add_argument("--days",  type=int, default=3,
                        help="Number of days per source to composite (default 3).")
    parser.add_argument("--width",  type=int, default=4096)
    parser.add_argument("--height", type=int, default=2048)
    parser.add_argument("--output", default=None)
    parser.add_argument("--no-mask",     action="store_true", help="Skip cloud mask generation.")
    parser.add_argument("--no-gap-fill", action="store_true", help="Skip gap fill pass.")
    args = parser.parse_args()

    source_names = [s.strip() for s in args.sources.split(",")]
    for s in source_names:
        if s not in LAYERS:
            print(f"ERROR: unknown source '{s}'. Choices: {list(LAYERS.keys())}", file=sys.stderr)
            return 1

    # Auto-detect date using the first source
    if args.date is None:
        print("Auto-detecting most recent available date...")
        args.date = find_recent_date(LAYERS[source_names[0]], args.width, args.height)

    base_date = datetime.date.fromisoformat(args.date)
    dates = [base_date - datetime.timedelta(days=i) for i in range(args.days)]

    if args.output is None:
        args.output = os.path.join(
            os.path.dirname(__file__), "output",
            f"CloudGlobal_{args.date}_{args.days}day.png",
        )
    mask_path = os.path.splitext(args.output)[0] + "_CloudMask.png"

    print(f"Sources: {', '.join(source_names)}")
    print(f"Dates:   {', '.join(d.isoformat() for d in dates)}")
    print(f"Frames:  {len(source_names) * len(dates)} total")
    print(f"Size:    {args.width}x{args.height} equirectangular")
    os.makedirs(os.path.dirname(args.output), exist_ok=True)

    # Fetch all source×date combinations
    frames = []
    for src in source_names:
        layer_id = LAYERS[src]
        for d in dates:
            ds  = d.isoformat()
            url = build_url(layer_id, args.width, args.height, ds)
            print(f"  [{src}] {ds}...", end=" ", flush=True)
            try:
                raw = fetch_bytes(url)
                w, h, rgba = decode_png(raw)
                frames.append((w, h, rgba))
                print(f"OK ({len(raw)/1024/1024:.1f} MB)")
            except Exception as e:
                print(f"SKIP ({e})")
            time.sleep(0.2)

    if not frames:
        print("ERROR: all fetches failed", file=sys.stderr)
        return 1

    print(f"Compositing {len(frames)} frames (max pixel)...")
    w, h, rgba = composite_max(frames)

    if not args.no_gap_fill:
        print("Gap fill (scanline propagation)...", end=" ", flush=True)
        gap_fill(rgba, w, h)
        print("done")

    out_bytes = encode_png_rgba(w, h, rgba)
    with open(args.output, "wb") as f:
        f.write(out_bytes)
    print(f"TrueColor → {args.output} ({len(out_bytes)/1024/1024:.1f} MB)")

    if not args.no_mask:
        print("Extracting cloud mask...", end=" ", flush=True)
        gray = make_cloud_mask(rgba)
        mask_bytes = encode_png_gray(w, h, gray)
        with open(mask_path, "wb") as f:
            f.write(mask_bytes)
        print(f"OK")
        print(f"CloudMask → {mask_path} ({len(mask_bytes)/1024/1024:.1f} MB)")

    print("\nUE import: Compression=Grayscale | sRGB=OFF | X=Wrap | Y=Clamp | Mips=ON")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
