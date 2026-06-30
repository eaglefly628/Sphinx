"""
Tile source: load a real satellite tile if the user dropped one in input/,
otherwise synthesise a deterministic "satellite-like" city tile.

The whole pipeline is source-agnostic: it only consumes an RGB array plus a
TileGeo anchor.  To use a real tile, drop `input/tile.png` (square) and an
optional `input/tile.json` ({"origin_lon":..,"origin_lat":..,"size_m":..}).

NOTE: the egress proxy in the remote environment blocks satellite tile servers
(arcgisonline / google / osm -> HTTP 403), so live fetching is not possible
here.  The synthetic generator is a stand-in only; it is NOT real imagery.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Optional, Tuple

import numpy as np
from PIL import Image

from .geo import TileGeo

# Default anchor: central Shanghai, matching Tests/MockData/Region_Shanghai.
DEFAULT_ORIGIN_LON = 121.4700
DEFAULT_ORIGIN_LAT = 31.2300
DEFAULT_SIZE_M = 256.0
DEFAULT_PX = 512  # source pixels -> 0.5 m/px before super-resolution


def _fractal_noise(shape: Tuple[int, int], rng: np.random.Generator,
                   octaves: int = 5, persistence: float = 0.55) -> np.ndarray:
    """Value-noise fractal in [0,1], built by upsampling coarse random grids."""
    h, w = shape
    out = np.zeros((h, w), dtype=np.float32)
    amp = 1.0
    total = 0.0
    for o in range(octaves):
        cells = 2 ** (o + 1)
        coarse = rng.random((cells + 1, cells + 1)).astype(np.float32)
        img = np.array(Image.fromarray((coarse * 255).astype(np.uint8))
                       .resize((w, h), Image.BICUBIC), dtype=np.float32) / 255.0
        out += amp * img
        total += amp
        amp *= persistence
    out /= max(total, 1e-6)
    return np.clip(out, 0.0, 1.0)


def _tint(base: Tuple[int, int, int], noise: np.ndarray, spread: float) -> np.ndarray:
    """Broadcast an RGB base colour modulated by a single-channel noise map."""
    mod = (1.0 - spread) + spread * noise[..., None]
    col = np.array(base, dtype=np.float32)[None, None, :]
    return np.clip(col * mod, 0, 255)


def synthesize_city_tile(px: int = DEFAULT_PX, seed: int = 7
                         ) -> Tuple[np.ndarray, np.ndarray]:
    """Return (rgb uint8 [px,px,3], truth_label int [px,px]).

    truth_label is for optional validation only; segmentation runs on RGB.
    Label ids: 0 grass, 1 tree, 2 road(asphalt), 3 concrete, 4 building,
               5 water, 6 soil, 7 object.
    """
    rng = np.random.default_rng(seed)
    rgb = np.zeros((px, px, 3), dtype=np.float32)
    label = np.full((px, px), 6, dtype=np.int32)  # default: soil

    fine = _fractal_noise((px, px), rng, octaves=6)
    coarse = _fractal_noise((px, px), rng, octaves=3)

    # 1) soil base everywhere
    rgb[:] = _tint((120, 96, 70), fine, 0.35)

    # 2) road grid (asphalt) + concrete sidewalks
    road_w = max(6, px // 36)
    walk_w = max(2, road_w // 3)
    centers = list(range(px // 6, px, px // 3))
    asphalt = _tint((60, 62, 66), fine, 0.18)
    concrete = _tint((165, 165, 160), fine, 0.20)
    for c in centers:
        for axis in (0, 1):
            lo, hi = c - road_w // 2, c + road_w // 2
            wlo, whi = lo - walk_w, hi + walk_w
            if axis == 0:  # horizontal strip (rows)
                rgb[wlo:whi, :] = concrete[wlo:whi, :]
                label[wlo:whi, :] = 3
                rgb[lo:hi, :] = asphalt[lo:hi, :]
                label[lo:hi, :] = 2
            else:          # vertical strip (cols)
                rgb[:, wlo:whi] = concrete[:, wlo:whi]
                label[:, wlo:whi] = 3
                rgb[:, lo:hi] = asphalt[:, lo:hi]
                label[:, lo:hi] = 2
    # lane markings on horizontal roads
    for c in centers:
        mark = np.zeros(px, dtype=bool)
        mark[::max(8, px // 32)] = True
        seg = (np.arange(px) % (px // 16)) < (px // 32)
        rgb[c - 1:c + 1, seg] = np.array([210, 200, 120], np.float32)

    # 3) blocks between roads -> roofs / parking / park / water
    bounds = [0] + [c for c in centers] + [px]
    block_kinds = ["park", "roof", "roof", "parking", "roof", "water",
                   "roof", "park", "roof", "parking", "roof", "roof",
                   "park", "roof", "roof", "roof"]
    ki = 0
    for bi in range(len(bounds) - 1):
        for bj in range(len(bounds) - 1):
            r0, r1 = bounds[bi] + road_w, bounds[bi + 1] - road_w
            c0, c1 = bounds[bj] + road_w, bounds[bj + 1] - road_w
            if r1 - r0 < 8 or c1 - c0 < 8:
                continue
            kind = block_kinds[ki % len(block_kinds)]
            ki += 1
            sub = (slice(r0, r1), slice(c0, c1))
            hh, ww = r1 - r0, c1 - c0
            if kind == "roof":
                roof_palette = [(150, 80, 70), (130, 130, 135), (180, 175, 165),
                                (95, 110, 120), (160, 120, 90)]
                base = roof_palette[rng.integers(len(roof_palette))]
                rgb[sub] = _tint(base, fine[sub], 0.16)
                label[sub] = 4
            elif kind == "parking":
                rgb[sub] = _tint((70, 72, 76), fine[sub], 0.14)
                label[sub] = 2
                # parking line markings
                rgb[r0:r1:max(6, hh // 10), c0:c1] = np.array([200, 200, 190], np.float32)
            elif kind == "park":
                gmix = 0.6 * fine[sub] + 0.4 * coarse[sub]
                rgb[sub] = _tint((86, 132, 64), gmix, 0.45)
                label[sub] = 0
                # tree canopies as dark-green blobs
                ntrees = max(3, (hh * ww) // 2200)
                yy, xx = np.mgrid[0:hh, 0:ww]
                for _ in range(ntrees):
                    ty, tx = rng.integers(hh), rng.integers(ww)
                    rad = rng.integers(max(3, hh // 14), max(5, hh // 8))
                    m = (yy - ty) ** 2 + (xx - tx) ** 2 < rad ** 2
                    shade = _tint((42, 86, 44), fine[sub], 0.5)
                    rgb[sub][m] = shade[m]
                    lbl = label[sub]; lbl[m] = 1; label[sub] = lbl
            elif kind == "water":
                # elliptical pond inside the block
                yy, xx = np.mgrid[0:hh, 0:ww]
                cy, cx = hh / 2, ww / 2
                m = ((yy - cy) / (hh * 0.42)) ** 2 + ((xx - cx) / (ww * 0.42)) ** 2 < 1
                rgb[sub] = _tint((96, 150, 70), fine[sub], 0.4)  # grassy verge
                label[sub] = 0
                water = _tint((40, 78, 120), coarse[sub], 0.22)
                blk = rgb[sub]; blk[m] = water[m]; rgb[sub] = blk
                lbl = label[sub]; lbl[m] = 5; label[sub] = lbl

    # 4) scatter small "not-very-tall" objects (bins, playground props)
    obj_colors = [(220, 60, 50), (240, 200, 40), (40, 120, 220), (235, 235, 235)]
    n_obj = 60
    yy, xx = np.mgrid[0:px, 0:px]
    for _ in range(n_obj):
        oy, ox = rng.integers(px), rng.integers(px)
        if label[oy, ox] in (5,):  # not on water
            continue
        rad = rng.integers(2, 4)
        m = (yy - oy) ** 2 + (xx - ox) ** 2 < rad ** 2
        col = np.array(obj_colors[rng.integers(len(obj_colors))], np.float32)
        rgb[m] = col
        label[m] = 7

    rgb = np.clip(rgb, 0, 255).astype(np.uint8)
    return rgb, label


def load_or_synthesize(input_dir: Path
                       ) -> Tuple[np.ndarray, TileGeo, dict, Optional[np.ndarray]]:
    """Return (rgb, TileGeo, source_meta, truth_label_or_None)."""
    input_dir = Path(input_dir)
    img_path = None
    for name in ("tile.png", "tile.jpg", "tile.jpeg", "tile.tif", "tile.tiff"):
        p = input_dir / name
        if p.exists():
            img_path = p
            break

    if img_path is not None:
        img = Image.open(img_path).convert("RGB")
        if img.width != img.height:
            side = min(img.width, img.height)
            img = img.crop((0, 0, side, side))
        rgb = np.array(img, dtype=np.uint8)
        meta_path = input_dir / "tile.json"
        if meta_path.exists():
            j = json.loads(meta_path.read_text())
            olon = float(j.get("origin_lon", DEFAULT_ORIGIN_LON))
            olat = float(j.get("origin_lat", DEFAULT_ORIGIN_LAT))
            size_m = float(j.get("size_m", DEFAULT_SIZE_M))
        else:
            olon, olat, size_m = DEFAULT_ORIGIN_LON, DEFAULT_ORIGIN_LAT, DEFAULT_SIZE_M
        geo = TileGeo(olon, olat, size_m, rgb.shape[0])
        source = {"kind": "user", "path": str(img_path.name),
                  "note": "Real tile provided by user."}
        return rgb, geo, source, None

    # No real tile: synthesise.
    rgb, truth = synthesize_city_tile(px=DEFAULT_PX, seed=7)
    geo = TileGeo(DEFAULT_ORIGIN_LON, DEFAULT_ORIGIN_LAT, DEFAULT_SIZE_M, DEFAULT_PX)
    source = {
        "kind": "synthetic",
        "note": ("Procedural stand-in -- egress proxy blocks satellite tile "
                 "servers (403). Drop input/tile.png to use a real tile."),
        "seed": 7,
    }
    return rgb, geo, source, truth
