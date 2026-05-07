#!/usr/bin/env python3
"""
Fetch global cloud cover from NASA GIBS MODIS Terra + Aqua TrueColor.

3-day priority composite (Terra + Aqua × 3 days = 6 frames):
  - Newest frame has highest priority.
  - Older frames fill only the pure-black swath gaps from newer frames.
  - Result: genuinely global cloud field with no "all clouds" bias.

Cloud mask: min(R,G,B)/255
  - White/grey clouds  → high value  ✓
  - Blue ocean / land  → low value   ✓

Usage:
    python3 fetch_gibs_cloud.py
    python3 fetch_gibs_cloud.py --date 2024-04-28
    python3 fetch_gibs_cloud.py --width 8192 --height 4096
    python3 fetch_gibs_cloud.py --days 5 --blur-radius 0

Output (in output/ dir):
    CloudGlobal_MODIS_<date>.png           TrueColor composite
    CloudGlobal_MODIS_<date>_CloudMask.png Grayscale cloud density (for UDS)

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

MODIS_LAYERS = [
    ("MODIS_Terra_CorrectedReflectance_TrueColor", "Terra"),
    ("MODIS_Aqua_CorrectedReflectance_TrueColor",  "Aqua"),
]


# ---------------------------------------------------------------------------
# Network
# ---------------------------------------------------------------------------

def build_url(layer, width, height, date_str):
    return GIBS_WMS + "?" + urllib.parse.urlencode({
        "SERVICE": "WMS", "REQUEST": "GetMap", "VERSION": "1.1.1",
        "LAYERS": layer, "STYLES": "", "FORMAT": "image/png",
        "SRS": "EPSG:4326", "WIDTH": str(width), "HEIGHT": str(height),
        "BBOX": "-180,-90,180,90", "TIME": date_str,
    })


def fetch_bytes(url, timeout=90.0, retries=3):
    req = urllib.request.Request(url, headers={"User-Agent": "EagleCloud/1.0"})
    delay = 2
    for attempt in range(retries + 1):
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                data = r.read()
            if not data.startswith(b"\x89PNG\r\n\x1a\n"):
                raise RuntimeError(f"Not PNG: {data[:120]!r}")
            return data
        except Exception as e:
            if attempt == retries:
                raise
            print(f"retry {attempt+1}/{retries} ({e})...", end=" ", flush=True)
            time.sleep(delay)
            delay *= 2


# ---------------------------------------------------------------------------
# PNG codec — handles 8-bit Grayscale / RGB / GrayA / RGBA
# ---------------------------------------------------------------------------

def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def decode_png(data):
    """Return (w, h, rgba_bytearray). Supports color types 0/2/4/6."""
    pos, ihdr, idat = 8, None, []
    while pos < len(data):
        n   = struct.unpack(">I", data[pos:pos+4])[0]
        tag = data[pos+4:pos+8]
        if tag == b"IHDR": ihdr = data[pos+8:pos+8+n]
        elif tag == b"IDAT": idat.append(data[pos+8:pos+8+n])
        elif tag == b"IEND": break
        pos += 12 + n

    w, h    = struct.unpack(">II", ihdr[:8])
    bd, ct  = ihdr[8], ihdr[9]
    assert bd == 8 and ct in (0,2,4,6), f"Unsupported PNG bd={bd} ct={ct}"
    bpp     = {0:1, 2:3, 4:2, 6:4}[ct]
    stride  = w * bpp
    raw     = zlib.decompress(b"".join(idat))
    out     = bytearray(w * h * 4)
    prev    = bytes(stride)

    for y in range(h):
        rs = y * (stride + 1)
        ft = raw[rs]
        row = bytearray(raw[rs+1:rs+1+stride])
        if ft == 1:
            for i in range(bpp, stride): row[i] = (row[i] + row[i-bpp]) & 0xFF
        elif ft == 2:
            for i in range(stride): row[i] = (row[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                a = row[i-bpp] if i >= bpp else 0
                row[i] = (row[i] + (a + prev[i]) // 2) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = row[i-bpp] if i >= bpp else 0
                c = prev[i-bpp] if i >= bpp else 0
                row[i] = (row[i] + _paeth(a, prev[i], c)) & 0xFF
        prev = bytes(row)
        bo = y * w * 4
        for x in range(w):
            sx = x * bpp
            if ct == 0:   v = row[sx];  out[bo+x*4:bo+x*4+4] = bytes([v,v,v,255])
            elif ct == 2: out[bo+x*4:bo+x*4+4] = bytes([row[sx],row[sx+1],row[sx+2],255])
            elif ct == 4: v = row[sx];  out[bo+x*4:bo+x*4+4] = bytes([v,v,v,row[sx+1]])
            elif ct == 6: out[bo+x*4:bo+x*4+4] = bytes(row[sx:sx+4])
    return w, h, out


def _chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag+data) & 0xFFFFFFFF)

def encode_png_rgba(w, h, rgba):
    raw = bytearray()
    for y in range(h): raw.append(0); raw.extend(rgba[y*w*4:(y+1)*w*4])
    return b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", struct.pack(">IIBBBBB",w,h,8,6,0,0,0)) + _chunk(b"IDAT", zlib.compress(bytes(raw),6)) + _chunk(b"IEND",b"")

def encode_png_gray(w, h, gray):
    raw = bytearray()
    for y in range(h): raw.append(0); raw.extend(gray[y*w:(y+1)*w])
    return b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", struct.pack(">IIBBBBB",w,h,8,0,0,0,0)) + _chunk(b"IDAT", zlib.compress(bytes(raw),6)) + _chunk(b"IEND",b"")


# ---------------------------------------------------------------------------
# Post-processing
# ---------------------------------------------------------------------------

def composite_priority(frames):
    """Newest-first priority: only fill pure-black (no-data) pixels from older frames."""
    w, h, result = frames[0]; result = bytearray(result)
    for _, _, frame in frames[1:]:
        for i in range(0, w*h*4, 4):
            if result[i] < 4 and result[i+1] < 4 and result[i+2] < 4:
                if not (frame[i] < 4 and frame[i+1] < 4 and frame[i+2] < 4):
                    result[i:i+4] = frame[i:i+4]
    return w, h, result


def box_blur(rgba, w, h, radius):
    if radius <= 0: return
    tmp = bytearray(len(rgba))
    for y in range(h):
        base = y*w*4
        for c in range(3):
            p = [0]*(w+1)
            for x in range(w): p[x+1] = p[x] + rgba[base+x*4+c]
            for x in range(w):
                lo,hi = max(0,x-radius), min(w-1,x+radius)
                tmp[base+x*4+c] = (p[hi+1]-p[lo])//(hi-lo+1)
        for x in range(w): tmp[base+x*4+3] = rgba[base+x*4+3]
    for x in range(w):
        for c in range(3):
            p = [0]*(h+1)
            for y in range(h): p[y+1] = p[y] + tmp[(y*w+x)*4+c]
            for y in range(h):
                lo,hi = max(0,y-radius), min(h-1,y+radius)
                rgba[(y*w+x)*4+c] = (p[hi+1]-p[lo])//(hi-lo+1)


def make_cloud_mask(rgba):
    n = len(rgba)//4; gray = bytearray(n)
    for i in range(n): b=i*4; gray[i]=min(rgba[b],rgba[b+1],rgba[b+2])
    return gray


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--date", default=None, help="YYYY-MM-DD UTC (default: yesterday)")
    parser.add_argument("--days",  type=int, default=3, help="Days to composite (default 3)")
    parser.add_argument("--width",  type=int, default=4096)
    parser.add_argument("--height", type=int, default=2048)
    parser.add_argument("--output", default=None)
    parser.add_argument("--blur-radius", type=int, default=3)
    parser.add_argument("--no-mask",     action="store_true")
    args = parser.parse_args()

    today = datetime.datetime.now(datetime.timezone.utc).date()
    # Default to yesterday so GIBS always has the data fully processed
    if args.date:
        end_date = datetime.date.fromisoformat(args.date)
    else:
        end_date = today - datetime.timedelta(days=1)

    out_dir = os.path.join(os.path.dirname(__file__), "output")
    os.makedirs(out_dir, exist_ok=True)

    dates = [end_date - datetime.timedelta(days=i) for i in range(args.days)]
    print(f"MODIS composite  {dates[-1]} → {dates[0]}  |  {args.width}x{args.height}")

    # Fetch order: newest Terra, newest Aqua, then older days interleaved
    # composite_priority uses frame[0] as highest priority → newest data wins
    frames = []
    for d in dates:
        for layer, label in MODIS_LAYERS:
            t = d.isoformat()
            url = build_url(layer, args.width, args.height, t)
            print(f"  [{label} {t}]...", end=" ", flush=True)
            try:
                raw = fetch_bytes(url)
                frames.append(decode_png(raw))
                kb = len(raw) // 1024
                print(f"OK ({kb/1024:.1f}MB)" if kb >= 1024 else f"OK ({kb}KB)")
            except Exception as e:
                print(f"SKIP ({e})")
            time.sleep(0.2)

    if not frames:
        print("ERROR: all fetches failed", file=sys.stderr)
        return 1

    out_path  = args.output or os.path.join(out_dir, f"CloudGlobal_MODIS_{end_date}.png")
    mask_path = os.path.splitext(out_path)[0] + "_CloudMask.png"

    print(f"Compositing {len(frames)} frames...")
    w, h, rgba = composite_priority(frames)

    if args.blur_radius > 0:
        print(f"Blur r={args.blur_radius}...", end=" ", flush=True)
        box_blur(rgba, w, h, args.blur_radius); print("done")

    with open(out_path, "wb") as f: f.write(encode_png_rgba(w, h, rgba))
    print(f"TrueColor → {out_path}")

    if not args.no_mask:
        with open(mask_path, "wb") as f: f.write(encode_png_gray(w, h, make_cloud_mask(rgba)))
        print(f"CloudMask → {mask_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
