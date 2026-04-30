#!/usr/bin/env python3
"""
Fetch global cloud cover image from NASA GIBS WMS endpoint.

Output: equirectangular PNG (lon -180..180, lat -90..90), full globe.
Usage:
    python3 fetch_gibs_cloud.py                        # latest, default layer/size
    python3 fetch_gibs_cloud.py --date 2024-04-28
    python3 fetch_gibs_cloud.py --layer VIIRS --width 8192 --height 4096

Layer choices (--layer):
    TerraTrueColor  (default) MODIS_Terra_CorrectedReflectance_TrueColor
                              ~10:30 local sun-sync, RGB true color, daily
    AquaTrueColor             MODIS_Aqua_CorrectedReflectance_TrueColor
                              ~13:30 local, RGB true color, daily
    VIIRS                     VIIRS_SNPP_CorrectedReflectance_TrueColor
                              Higher res, RGB true color, daily
    CloudFraction             MODIS_Terra_Cloud_Fraction_Day
                              Palette-encoded cloud %, scientific layer

True Color layers contain land + ocean + clouds together. For our use:
clouds are the "white & low-saturation" pixels — UDS material can extract
density via brightness thresholding. Or use CloudFraction directly if you
want clean cloud-only data (but it's palette-colored, harder to consume).

Stdlib only (urllib + zlib).
"""

import argparse
import datetime
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


GIBS_WMS = "https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi"

LAYERS = {
    "TerraTrueColor":  "MODIS_Terra_CorrectedReflectance_TrueColor",
    "AquaTrueColor":   "MODIS_Aqua_CorrectedReflectance_TrueColor",
    "VIIRS":           "VIIRS_SNPP_CorrectedReflectance_TrueColor",
    "CloudFraction":   "MODIS_Terra_Cloud_Fraction_Day",
}


def build_url(layer_id: str, width: int, height: int, date_str: str) -> str:
    params = {
        "SERVICE": "WMS",
        "REQUEST": "GetMap",
        "VERSION": "1.1.1",  # 1.1.1 uses lon,lat bbox order (clearer than 1.3.0)
        "LAYERS": layer_id,
        "STYLES": "",
        "FORMAT": "image/png",
        "SRS": "EPSG:4326",
        "WIDTH": str(width),
        "HEIGHT": str(height),
        "BBOX": "-180,-90,180,90",  # lon_min,lat_min,lon_max,lat_max (full globe)
        "TIME": date_str,
    }
    return GIBS_WMS + "?" + urllib.parse.urlencode(params)


def fetch(url: str, out_path: str, timeout: float = 60.0) -> int:
    req = urllib.request.Request(url, headers={"User-Agent": "EagleCloud/0.1"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        if resp.status != 200:
            raise RuntimeError(f"HTTP {resp.status}")
        data = resp.read()
    # GIBS returns image/png even on errors sometimes (with text);
    # check signature
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        # Could be an XML ServiceException
        snippet = data[:500].decode("utf-8", errors="replace")
        raise RuntimeError(f"Response not PNG. Body starts with:\n{snippet}")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(data)
    return len(data)


def find_recent_date(layer_id: str, width: int, height: int, max_days_back: int = 5) -> str:
    """Try today UTC, fall back day-by-day until we get valid PNG."""
    today = datetime.datetime.utcnow().date()
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
    # Best effort: just try today
    return today.isoformat()


def main() -> int:
    parser = argparse.ArgumentParser(description="Download global cloud cover from NASA GIBS.")
    parser.add_argument("--layer", choices=list(LAYERS.keys()), default="TerraTrueColor",
                        help="GIBS layer (default: TerraTrueColor)")
    parser.add_argument("--date", default=None,
                        help="Date YYYY-MM-DD (UTC). Default: most recent available.")
    parser.add_argument("--width", type=int, default=4096, help="PNG width (default 4096)")
    parser.add_argument("--height", type=int, default=2048, help="PNG height (default 2048)")
    parser.add_argument("--output", default=None,
                        help="Output PNG path. Default: output/CloudGlobal_<layer>_<date>.png")
    args = parser.parse_args()

    layer_id = LAYERS[args.layer]

    if args.date is None:
        print("Auto-detecting most recent available date...")
        args.date = find_recent_date(layer_id, args.width, args.height)
    print(f"Layer: {args.layer} ({layer_id})")
    print(f"Date:  {args.date}")
    print(f"Size:  {args.width}x{args.height} equirectangular (full globe)")

    if args.output is None:
        args.output = os.path.join(
            os.path.dirname(__file__), "output",
            f"CloudGlobal_{args.layer}_{args.date}.png",
        )

    url = build_url(layer_id, args.width, args.height, args.date)
    print(f"Fetching: {url}")
    try:
        size_bytes = fetch(url, args.output)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1

    print(f"Wrote {args.output} ({size_bytes / 1024 / 1024:.1f} MB)")
    print()
    print("Next steps:")
    print("  1. Import into UE Content browser -> creates UTexture2D")
    print("  2. Set texture compression: 'HDR' or 'Default (RGBA)' (NOT BC1 - kills clouds)")
    print("  3. Set sRGB = false  (UDS reads linear cloud values)")
    print("  4. Phase B/C integration: feed into UDS Static Clouds Texture for")
    print("     global sky wrap, or sample by camera lat/lon into Cloud Coverage RT")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
