#!/usr/bin/env python3
"""
Fetch a real satellite tile for the pipeline (run this on YOUR machine --
the remote sandbox blocks tile servers).

Stitches XYZ/WMTS tiles around a lon/lat anchor into input/tile.png and
writes input/tile.json (SW-corner anchor + physical size) in the exact form
tile_source.load_or_synthesize() expects.  Then just run pipeline.py.

Providers:
    esri      Esri World Imagery. No key. ~0.3-0.6 m/px in cities. WGS84.
              Attribution required for public use.
    tianditu  天地图 (需免费 key: https://console.tianditu.gov.cn 申请, --key).
              国产合规首选, 最高 z18 (~0.6 m/px), CGCS2000≈WGS84.

Examples:
    python3 fetch_tile.py --lon 121.47 --lat 31.23 --zoom 18 --n 4
    python3 fetch_tile.py --provider tianditu --key YOUR_TK --zoom 17 --n 4
"""
from __future__ import annotations

import argparse
import io
import json
import math
import time
import urllib.request
from pathlib import Path

from PIL import Image

PROVIDERS = {
    "esri": ("https://server.arcgisonline.com/ArcGIS/rest/services/"
             "World_Imagery/MapServer/tile/{z}/{y}/{x}"),
    "tianditu": ("https://t{s}.tianditu.gov.cn/img_w/wmts?SERVICE=WMTS"
                 "&REQUEST=GetTile&VERSION=1.0.0&LAYER=img&STYLE=default"
                 "&TILEMATRIXSET=w&FORMAT=tiles&TILEMATRIX={z}"
                 "&TILEROW={y}&TILECOL={x}&tk={key}"),
}
ATTRIBUTION = {
    "esri": "Esri, Maxar, Earthstar Geographics, and the GIS User Community",
    "tianditu": "天地图 National Platform for Common Geospatial Information Services",
}


def lonlat_to_tile(lon: float, lat: float, z: int) -> tuple[int, int]:
    n = 2 ** z
    x = int((lon + 180.0) / 360.0 * n)
    r = math.radians(lat)
    y = int((1.0 - math.log(math.tan(r) + 1.0 / math.cos(r)) / math.pi) / 2.0 * n)
    return x, y


def tile_to_lonlat(x: int, y: int, z: int) -> tuple[float, float]:
    """NW corner of tile (x, y)."""
    n = 2 ** z
    lon = x / n * 360.0 - 180.0
    lat = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * y / n))))
    return lon, lat


def fetch(url: str, retries: int = 3) -> Image.Image:
    req = urllib.request.Request(url, headers={"User-Agent": "SphinxSatGround/1.0"})
    for i in range(retries):
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                return Image.open(io.BytesIO(r.read())).convert("RGB")
        except Exception:
            if i == retries - 1:
                raise
            time.sleep(1.5 * (i + 1))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--provider", choices=PROVIDERS, default="esri")
    ap.add_argument("--key", default="", help="tianditu tk")
    ap.add_argument("--lon", type=float, default=121.47)
    ap.add_argument("--lat", type=float, default=31.23)
    ap.add_argument("--zoom", type=int, default=18,
                    help="17~1.0 m/px, 18~0.5, 19~0.25 (at 31N)")
    ap.add_argument("--n", type=int, default=4, help="stitch n x n tiles (256px each)")
    ap.add_argument("--out", default="input")
    args = ap.parse_args()

    if args.provider == "tianditu" and not args.key:
        ap.error("tianditu 需要 --key（tk），在 console.tianditu.gov.cn 免费申请")

    cx, cy = lonlat_to_tile(args.lon, args.lat, args.zoom)
    x0, y0 = cx - args.n // 2, cy - args.n // 2
    canvas = Image.new("RGB", (256 * args.n, 256 * args.n))
    for dy in range(args.n):
        for dx in range(args.n):
            url = PROVIDERS[args.provider].format(
                z=args.zoom, x=x0 + dx, y=y0 + dy, key=args.key,
                s=(dx + dy) % 8)
            canvas.paste(fetch(url), (dx * 256, dy * 256))
            print(f"  tile {dy * args.n + dx + 1}/{args.n * args.n}", end="\r")
    print()

    # SW anchor = NW corner of the tile row BELOW the block's last row.
    west, north = tile_to_lonlat(x0, y0, args.zoom)
    _, south = tile_to_lonlat(x0, y0 + args.n, args.zoom)
    east, _ = tile_to_lonlat(x0 + args.n, y0, args.zoom)
    # physical size: metres of the block's east-west extent at its mid-latitude
    mid = math.radians((north + south) / 2.0)
    size_m = (east - west) * 111320.0 * math.cos(mid)

    out = Path(__file__).parent / args.out
    out.mkdir(parents=True, exist_ok=True)
    canvas.save(out / "tile.png")
    (out / "tile.json").write_text(json.dumps({
        "origin_lon": west, "origin_lat": south, "size_m": round(size_m, 2),
        "provider": args.provider, "zoom": args.zoom,
        "attribution": ATTRIBUTION[args.provider],
    }, indent=2))
    print(f"saved {out/'tile.png'} ({canvas.size[0]}px, {size_m:.0f} m, "
          f"{size_m / canvas.size[0]:.2f} m/px)\nnow run: python3 pipeline.py")


if __name__ == "__main__":
    main()
