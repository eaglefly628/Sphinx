#!/usr/bin/env python3
"""
Fetch global cloud cover image from NASA GIBS WMS endpoint.

Default: geostationary composite (GOES-East + GOES-West + Himawari).
  - Each satellite covers its hemisphere completely — no orbital swath gaps.
  - Single day fetch, no multi-day stacking needed.
  - Europe/Africa gap (~15°W–80°E) filled by scanline propagation.
  - Color doesn't matter: we extract a grayscale CloudMask for UDS anyway.

Usage:
    python3 fetch_gibs_cloud.py                    # geostationary (default)
    python3 fetch_gibs_cloud.py --modis            # fallback: MODIS 3-day composite
    python3 fetch_gibs_cloud.py --date 2024-04-28
    python3 fetch_gibs_cloud.py --width 8192 --height 4096
    python3 fetch_gibs_cloud.py --blur-radius 0    # skip blur

Geostationary sources (fixed, single-day):
    GOES-East  GOES-East_ABI_GeoColor         18:00 UTC  Americas
    GOES-West  GOES-West_ABI_GeoColor         20:00 UTC  Pacific / W. Americas
    Himawari   Himawari_AHI_Band3_Red_Visible_1km  01:00 UTC  Asia / Pacific
               (grayscale single-channel — fine for cloud mask)

MODIS sources (--modis flag, 3-day priority composite):
    TerraTrueColor / AquaTrueColor

Stdlib only (urllib + zlib + struct).
"""

import argparse
import datetime
import os
import struct
import sys
import time
import urllib.parse
import urllib.request
import zlib


GIBS_WMS = "https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi"

# Geostationary: (GIBS layer name, optimal UTC time HH:MM)
GEO_SOURCES = [
    ("GOES-East_ABI_GeoColor",             "18:00"),
    ("GOES-West_ABI_GeoColor",             "20:00"),
    ("Himawari_AHI_Band3_Red_Visible_1km", "01:00"),
]

# MODIS polar-orbit layers (--modis mode, multi-day composite)
MODIS_LAYERS = {
    "TerraTrueColor": "MODIS_Terra_CorrectedReflectance_TrueColor",
    "AquaTrueColor":  "MODIS_Aqua_CorrectedReflectance_TrueColor",
}


# ---------------------------------------------------------------------------
# Network
# ---------------------------------------------------------------------------

def build_url(layer: str, width: int, height: int, time_str: str) -> str:
    return GIBS_WMS + "?" + urllib.parse.urlencode({
        "SERVICE": "WMS", "REQUEST": "GetMap", "VERSION": "1.1.1",
        "LAYERS": layer, "STYLES": "", "FORMAT": "image/png",
        "SRS": "EPSG:4326", "WIDTH": str(width), "HEIGHT": str(height),
        "BBOX": "-180,-90,180,90", "TIME": time_str,
    })


def fetch_bytes(url: str, timeout: float = 90.0) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "EagleCloud/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        data = r.read()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise RuntimeError(f"Not PNG: {data[:120]!r}")
    return data


# ---------------------------------------------------------------------------
# PNG codec (8-bit RGB, RGBA, and Grayscale)
# ---------------------------------------------------------------------------

def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def decode_png(data: bytes):
    """Return (width, height, rgba_bytearray). Handles 8-bit L/LA/RGB/RGBA."""
    pos, ihdr_data, idat = 8, None, []
    while pos < len(data):
        n   = struct.unpack(">I", data[pos:pos+4])[0]
        tag = data[pos+4:pos+8]
        chunk = data[pos+8:pos+8+n]
        pos += 12 + n
        if tag == b"IHDR": ihdr_data = chunk
        elif tag == b"IDAT": idat.append(chunk)
        elif tag == b"IEND": break

    w, h      = struct.unpack(">II", ihdr_data[:8])
    bd, ct    = ihdr_data[8], ihdr_data[9]
    assert bd == 8, f"Only 8-bit PNG supported (got {bd})"
    # color types: 0=Gray, 2=RGB, 4=GrayA, 6=RGBA
    assert ct in (0, 2, 4, 6), f"Unsupported color type {ct}"

    src_bpp = {0: 1, 2: 3, 4: 2, 6: 4}[ct]
    stride  = w * src_bpp
    raw     = zlib.decompress(b"".join(idat))

    out      = bytearray(w * h * 4)
    prev_row = bytes(stride)
    for y in range(h):
        rs    = y * (stride + 1)
        ftype = raw[rs]
        row   = bytearray(raw[rs+1:rs+1+stride])
        bpp   = src_bpp
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
                a = row[i-bpp] if i >= bpp else 0
                c = prev_row[i-bpp] if i >= bpp else 0
                row[i] = (row[i] + _paeth(a, prev_row[i], c)) & 0xFF
        prev_row = bytes(row)

        bo = y * w * 4
        for x in range(w):
            sx = x * src_bpp
            if ct == 0:    # Grayscale → replicate to RGB
                v = row[sx]
                out[bo+x*4], out[bo+x*4+1], out[bo+x*4+2], out[bo+x*4+3] = v, v, v, 255
            elif ct == 2:  # RGB
                out[bo+x*4], out[bo+x*4+1], out[bo+x*4+2], out[bo+x*4+3] = row[sx], row[sx+1], row[sx+2], 255
            elif ct == 4:  # GrayA
                v = row[sx]
                out[bo+x*4], out[bo+x*4+1], out[bo+x*4+2], out[bo+x*4+3] = v, v, v, row[sx+1]
            elif ct == 6:  # RGBA
                out[bo+x*4], out[bo+x*4+1], out[bo+x*4+2], out[bo+x*4+3] = row[sx], row[sx+1], row[sx+2], row[sx+3]
    return w, h, out


def _chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag+data) & 0xFFFFFFFF)

def encode_png_rgba(w, h, rgba):
    raw = bytearray()
    for y in range(h):
        raw.append(0); raw.extend(rgba[y*w*4:(y+1)*w*4])
    return b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + _chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + _chunk(b"IEND", b"")

def encode_png_gray(w, h, gray):
    raw = bytearray()
    for y in range(h):
        raw.append(0); raw.extend(gray[y*w:(y+1)*w])
    return b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)) + _chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + _chunk(b"IEND", b"")


# ---------------------------------------------------------------------------
# Composite / post-process
# ---------------------------------------------------------------------------

def composite_priority(frames):
    """Newest-first: only fill pure-black (no-data) pixels from later frames."""
    w, h, result = frames[0]
    result = bytearray(result)
    for _, _, frame in frames[1:]:
        for i in range(0, w * h * 4, 4):
            if result[i] < 4 and result[i+1] < 4 and result[i+2] < 4:
                r2, g2, b2 = frame[i], frame[i+1], frame[i+2]
                if not (r2 < 4 and g2 < 4 and b2 < 4):
                    result[i], result[i+1], result[i+2], result[i+3] = r2, g2, b2, frame[i+3]
    return w, h, result


def gap_fill(rgba, w, h):
    """Scanline propagation: fill remaining black pixels from nearest neighbor."""
    def blk(i): return rgba[i] < 4 and rgba[i+1] < 4 and rgba[i+2] < 4
    for _ in range(2):
        for y in range(h):
            b = y * w * 4
            for x in range(1, w):
                i = b + x*4
                if blk(i): j = i-4; rgba[i],rgba[i+1],rgba[i+2] = rgba[j],rgba[j+1],rgba[j+2]
        for y in range(h):
            b = y * w * 4
            for x in range(w-2, -1, -1):
                i = b + x*4
                if blk(i): j = i+4; rgba[i],rgba[i+1],rgba[i+2] = rgba[j],rgba[j+1],rgba[j+2]
        for x in range(w):
            for y in range(1, h):
                i = (y*w+x)*4
                if blk(i): j = ((y-1)*w+x)*4; rgba[i],rgba[i+1],rgba[i+2] = rgba[j],rgba[j+1],rgba[j+2]
        for x in range(w):
            for y in range(h-2, -1, -1):
                i = (y*w+x)*4
                if blk(i): j = ((y+1)*w+x)*4; rgba[i],rgba[i+1],rgba[i+2] = rgba[j],rgba[j+1],rgba[j+2]


def box_blur(rgba, w, h, radius):
    """Separable box blur (H then V) to smooth satellite boundary seams."""
    if radius <= 0: return
    tmp = bytearray(len(rgba))
    for y in range(h):
        base = y * w * 4
        for c in range(3):
            prefix = [0] * (w + 1)
            for x in range(w): prefix[x+1] = prefix[x] + rgba[base + x*4 + c]
            for x in range(w):
                lo, hi = max(0, x-radius), min(w-1, x+radius)
                tmp[base + x*4 + c] = (prefix[hi+1] - prefix[lo]) // (hi-lo+1)
        for x in range(w): tmp[base + x*4 + 3] = rgba[base + x*4 + 3]
    for x in range(w):
        for c in range(3):
            prefix = [0] * (h + 1)
            for y in range(h): prefix[y+1] = prefix[y] + tmp[(y*w+x)*4 + c]
            for y in range(h):
                lo, hi = max(0, y-radius), min(h-1, y+radius)
                rgba[(y*w+x)*4 + c] = (prefix[hi+1] - prefix[lo]) // (hi-lo+1)


def make_cloud_mask(rgba):
    n = len(rgba) // 4
    gray = bytearray(n)
    for i in range(n):
        b = i * 4
        gray[i] = min(rgba[b], rgba[b+1], rgba[b+2])
    return gray


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Fetch global cloud cover from NASA GIBS.")
    parser.add_argument("--modis", action="store_true",
                        help="Use MODIS polar-orbit composite instead of geostationary.")
    parser.add_argument("--days", type=int, default=3,
                        help="MODIS mode: days to composite (default 3).")
    parser.add_argument("--date",  default=None, help="Date YYYY-MM-DD UTC (default: today).")
    parser.add_argument("--width",  type=int, default=4096)
    parser.add_argument("--height", type=int, default=2048)
    parser.add_argument("--output", default=None)
    parser.add_argument("--blur-radius", type=int, default=3)
    parser.add_argument("--no-mask",     action="store_true")
    parser.add_argument("--no-gap-fill", action="store_true")
    args = parser.parse_args()

    today = datetime.datetime.now(datetime.timezone.utc).date()
    date  = datetime.date.fromisoformat(args.date) if args.date else today

    out_dir = os.path.join(os.path.dirname(__file__), "output")
    os.makedirs(out_dir, exist_ok=True)

    frames = []

    if args.modis:
        # ---- MODIS polar-orbit: Terra + Aqua, N-day priority composite ----
        print(f"Mode: MODIS  ({args.days}-day priority composite, Terra+Aqua)")
        dates = [date - datetime.timedelta(days=i) for i in range(args.days)]
        for name, layer_id in MODIS_LAYERS.items():
            for d in dates:
                t = d.isoformat()
                url = build_url(layer_id, args.width, args.height, t)
                print(f"  [{name}] {t}...", end=" ", flush=True)
                try:
                    raw = fetch_bytes(url)
                    frames.append(decode_png(raw))
                    print(f"OK ({len(raw)//1024//1024}MB)")
                except Exception as e:
                    print(f"SKIP ({e})")
                time.sleep(0.2)
        tag = f"MODIS_{date}_{args.days}day"

    else:
        # ---- Geostationary: GOES-East + GOES-West + Himawari, single day ----
        print(f"Mode: Geostationary  (GOES-East + GOES-West + Himawari, {date})")
        # Himawari scans Asia at ~01:00 UTC, which is the *next* calendar day in UTC
        # for the date requested (e.g., "today in Asia" = yesterday UTC evening).
        # Use date-1 for Himawari so all three satellites cover the same solar day.
        himawari_date = date - datetime.timedelta(days=1)
        source_times = [
            (GEO_SOURCES[0][0], f"{date}T{GEO_SOURCES[0][1]}:00Z"),
            (GEO_SOURCES[1][0], f"{date}T{GEO_SOURCES[1][1]}:00Z"),
            (GEO_SOURCES[2][0], f"{himawari_date}T{GEO_SOURCES[2][1]}:00Z"),
        ]
        labels = ["GOES-East", "GOES-West", "Himawari"]
        for (layer, t), label in zip(source_times, labels):
            url = build_url(layer, args.width, args.height, t)
            print(f"  [{label}] {t}...", end=" ", flush=True)
            try:
                raw = fetch_bytes(url)
                frames.append(decode_png(raw))
                print(f"OK ({len(raw)//1024//1024}MB)")
            except Exception as e:
                print(f"SKIP ({e})")
            time.sleep(0.2)
        tag = f"GEO_{date}"

    if not frames:
        print("ERROR: all fetches failed", file=sys.stderr)
        return 1

    if args.output is None:
        args.output = os.path.join(out_dir, f"CloudGlobal_{tag}.png")
    mask_path = os.path.splitext(args.output)[0] + "_CloudMask.png"

    print(f"Compositing {len(frames)} frames...")
    w, h, rgba = composite_priority(frames)

    if not args.no_gap_fill:
        print("Gap fill...", end=" ", flush=True)
        gap_fill(rgba, w, h)
        print("done")

    if args.blur_radius > 0:
        print(f"Blur radius={args.blur_radius}...", end=" ", flush=True)
        box_blur(rgba, w, h, args.blur_radius)
        print("done")

    out_bytes = encode_png_rgba(w, h, rgba)
    with open(args.output, "wb") as f: f.write(out_bytes)
    print(f"TrueColor → {args.output}  ({len(out_bytes)//1024//1024}MB)")

    if not args.no_mask:
        print("Cloud mask...", end=" ", flush=True)
        gray = make_cloud_mask(rgba)
        mb = encode_png_gray(w, h, gray)
        with open(mask_path, "wb") as f: f.write(mb)
        print(f"OK")
        print(f"CloudMask → {mask_path}  ({len(mb)//1024//1024}MB)")

    print("\nUE import: sRGB=OFF | Grayscale | X=Wrap | Y=Clamp | Mips=ON")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
