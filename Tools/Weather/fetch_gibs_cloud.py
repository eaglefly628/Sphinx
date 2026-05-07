#!/usr/bin/env python3
"""
Fetch global merged geostationary IR cloud cover from NOAA nowCOAST WMS.

Data source: NOAA GMGSI (Global Mosaic of Geostationary Satellite Imagery)
  - Satellites: GOES-18 (East), GOES-19 (West), Himawari-9, Meteosat-9, Meteosat-10
  - Band: Longwave IR (~12 µm) — cloud tops are cold → bright pixel → high density
  - Coverage: 60°S to 60°N global, seamless, no swath gaps
  - Update: hourly, ~3 km resolution, no authentication required

Day AND Night: thermal IR does not depend on sunlight. No half-globe blackout.

Cloud mask: brightness/255
  - Cold high cloud (200–240 K) → bright white pixel → density ≈ 1
  - Warm surface / clear sky (280–310 K) → dark pixel → density ≈ 0

Channels available (--channel flag):
  lw   Longwave IR 12 µm   (best for cloud detection, default)
  sw   Shortwave IR 3.8 µm (sensitive to thin cirrus)
  wv   Water vapor 6.7 µm  (upper-troposphere moisture / cirrus)
  vis  Visible              (daytime only, no cloud at night)

Usage:
    python3 fetch_gibs_cloud.py
    python3 fetch_gibs_cloud.py --time 2024-04-28T18:00:00Z
    python3 fetch_gibs_cloud.py --width 8192 --height 4096
    python3 fetch_gibs_cloud.py --channel wv --blur-radius 0

Output (in output/ dir):
    CloudGlobal_GEO_<timestamp>.png           IR composite (grayscale)
    CloudGlobal_GEO_<timestamp>_CloudMask.png Grayscale cloud density (for UDS)

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


NOWCOAST_WMS = "https://nowcoast.noaa.gov/geoserver/satellite/wms"

# NOAA nowCOAST global merged GEO mosaic layers
CHANNELS = {
    "lw":  "global_longwave_imagery_mosaic",   # ~12 µm thermal IR (default)
    "sw":  "global_shortwave_imagery_mosaic",  # ~3.8 µm shortwave IR
    "wv":  "global_water_vapor_imagery_mosaic",# ~6.7 µm water vapor
    "vis": "global_visible_imagery_mosaic",    # visible (daytime only)
}


# ---------------------------------------------------------------------------
# Network
# ---------------------------------------------------------------------------

def build_url(layer, width, height, time_str=None):
    params = {
        "SERVICE": "WMS", "REQUEST": "GetMap", "VERSION": "1.3.0",
        "LAYERS": layer, "STYLES": "", "FORMAT": "image/png",
        "CRS": "CRS:84",  # lon,lat ordering — avoids WMS 1.3.0 axis-swap
        "WIDTH": str(width), "HEIGHT": str(height),
        "BBOX": "-180,-90,180,90",
    }
    if time_str:
        params["TIME"] = time_str
    return NOWCOAST_WMS + "?" + urllib.parse.urlencode(params)


def fetch_bytes(url, timeout=90.0, retries=3):
    req = urllib.request.Request(url, headers={"User-Agent": "EagleCloud/1.0"})
    delay = 2
    for attempt in range(retries + 1):
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                data = r.read()
            if not data.startswith(b"\x89PNG\r\n\x1a\n"):
                raise RuntimeError(f"Not PNG: {data[:200]!r}")
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
    # IR: cold cloud tops = bright pixel = high cloud density.
    # Take R channel (IR is grayscale; R=G=B for grayscale PNGs).
    n = len(rgba)//4; gray = bytearray(n)
    for i in range(n): gray[i] = rgba[i*4]
    return gray


def clear_polar_bands(rgba, w, h, max_lat_deg=60.0):
    """Zero out rows outside ±max_lat_deg (no-data garbage from WMS fill)."""
    # equirectangular: row 0 = 90°N, row h-1 = 90°S
    # row_cut_n = rows where lat > max_lat_deg (top of image)
    row_n = int(h * (90.0 - max_lat_deg) / 180.0)   # first valid row from top
    row_s = int(h * (90.0 + max_lat_deg) / 180.0)   # last valid row from top
    for y in list(range(0, row_n)) + list(range(row_s, h)):
        base = y * w * 4
        for x in range(w):
            rgba[base+x*4] = rgba[base+x*4+1] = rgba[base+x*4+2] = rgba[base+x*4+3] = 0


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Fetch NOAA global merged geostationary IR cloud cover.")
    parser.add_argument("--time",    default=None,
                        help="ISO 8601 UTC time, e.g. 2024-04-28T18:00:00Z (default: latest)")
    parser.add_argument("--channel", default="lw", choices=list(CHANNELS),
                        help="IR channel: lw=longwave(default) sw=shortwave wv=watervapor vis=visible")
    parser.add_argument("--width",   type=int, default=4096)
    parser.add_argument("--height",  type=int, default=2048)
    parser.add_argument("--output",  default=None)
    parser.add_argument("--blur-radius", type=int, default=2)
    parser.add_argument("--polar-lat",   type=float, default=60.0,
                        help="Zero out pixels above/below this latitude (default 60). "
                             "nowCOAST data ends at ±60°; set to 90 to keep polar fill.")
    parser.add_argument("--no-mask", action="store_true")
    args = parser.parse_args()

    layer = CHANNELS[args.channel]
    tag   = args.time.replace(":", "").replace("-", "") if args.time \
            else datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")

    out_dir = os.path.join(os.path.dirname(__file__), "output")
    os.makedirs(out_dir, exist_ok=True)

    out_path  = args.output or os.path.join(out_dir, f"CloudGlobal_GEO_{tag}.png")
    mask_path = os.path.splitext(out_path)[0] + "_CloudMask.png"

    url = build_url(layer, args.width, args.height, args.time)
    desc = f"[NOAA nowCOAST] {layer}"
    if args.time:
        desc += f" @ {args.time}"
    print(f"{desc}  {args.width}x{args.height}...", end=" ", flush=True)

    try:
        raw = fetch_bytes(url)
    except Exception as e:
        print(f"FAILED: {e}", file=sys.stderr)
        return 1

    kb = len(raw) // 1024
    print(f"OK ({kb/1024:.1f}MB)" if kb >= 1024 else f"OK ({kb}KB)")

    w, h, rgba = decode_png(raw)

    if args.polar_lat < 90.0:
        print(f"Clear polar bands (|lat| > {args.polar_lat}°)...", end=" ", flush=True)
        clear_polar_bands(rgba, w, h, args.polar_lat); print("done")

    if args.blur_radius > 0:
        print(f"Blur r={args.blur_radius}...", end=" ", flush=True)
        box_blur(rgba, w, h, args.blur_radius); print("done")

    with open(out_path, "wb") as f: f.write(encode_png_rgba(w, h, rgba))
    print(f"IR composite → {out_path}")

    if not args.no_mask:
        with open(mask_path, "wb") as f: f.write(encode_png_gray(w, h, make_cloud_mask(rgba)))
        print(f"CloudMask    → {mask_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
