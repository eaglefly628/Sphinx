"""
Segmentation stage: split the tile into material/land-cover classes and emit
soft "splat" weight maps that drive the layered ground material.

This is the real lever of the project (see design notes): the satellite image
gives macro layout + colour, segmentation says *which tiling PBR material* each
pixel should use.  Here we use an unsupervised path (KMeans in Lab+texture
feature space -> heuristic colour mapping) so it runs offline with no model
weights.  A learned semantic-segmentation backend (OpenEarthMap / LoveDA, or the
existing ESA WorldCover fusion) can replace `segment()` later -- the downstream
contract is just (class_map, splat, legend).

Class ids are kept consistent with tile_source truth labels:
  0 grass, 1 tree, 2 road, 3 concrete, 4 building, 5 water, 6 soil, 7 object
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import List, Tuple

import numpy as np
from scipy import ndimage
from skimage.color import rgb2lab, rgb2hsv
from sklearn.cluster import KMeans

CLASS_KEYS = ["grass", "tree", "road", "concrete", "building", "water", "soil", "object"]
CLASS_NAMES = ["Grassland", "Tree canopy", "Road / asphalt", "Concrete / pavement",
               "Building roof", "Water", "Bare soil", "Ground object"]
# Display colours for previews/legend (RGB 0-255).
CLASS_COLORS = [
    (110, 170, 80), (40, 95, 50), (70, 72, 78), (180, 180, 175),
    (170, 110, 95), (50, 110, 175), (140, 110, 80), (240, 70, 60),
]
N_CLASSES = len(CLASS_KEYS)


@dataclass
class SegResult:
    class_map: np.ndarray          # (H,W) int8 in 0..N_CLASSES-1
    splat: np.ndarray              # (H,W,N_CLASSES) float32, sums to 1 per pixel
    legend: List[dict]
    objects: List[dict]            # detected ground objects (pixel + geo centroid)


def _centroid_to_class(rgb_centroid: np.ndarray, texture: float) -> int:
    """Map a cluster's mean colour (+ texture) to a material class via HSV rules."""
    r8, g8, b8 = rgb_centroid  # 0-255
    r, g, b = rgb_centroid / 255.0
    h, s, v = rgb2hsv(np.array([[[r, g, b]]], dtype=np.float32))[0, 0]
    hue = h * 360.0
    # Water: clearly blue (strong blue margin), not just dark blue-grey asphalt.
    if 185.0 <= hue <= 260.0 and (b8 - r8) > 28.0 and s > 0.18:
        return 5
    # Vegetation: green hue.
    if 70.0 <= hue <= 175.0 and g >= r and g >= b * 0.9 and s > 0.12:
        return 1 if v < 0.42 else 0  # dark+ -> tree, else grass
    # Greyscale-ish surfaces (low saturation): split by brightness.
    if s < 0.18:
        if v < 0.34:
            return 2  # asphalt / road
        if v > 0.6:
            return 3  # concrete / pavement
        return 4      # mid grey -> building roof
    # Warm hues -> soil vs roof by saturation/brightness.
    if 15.0 <= hue <= 65.0:
        return 6 if s > 0.3 and v < 0.6 else 4
    # Fallback: building roof.
    return 4


def _detect_objects(rgb: np.ndarray, geo, pixel_side: int) -> Tuple[np.ndarray, List[dict]]:
    """Find small, vivid, isolated blobs -> 'ground object' class + centroids."""
    hsv = rgb2hsv(rgb.astype(np.float32) / 255.0)
    hue = hsv[..., 0] * 360.0
    s = hsv[..., 1]
    v = hsv[..., 2]
    vivid = (s > 0.45) & (v > 0.4)
    not_veg = ~((hue >= 70) & (hue <= 175))
    not_water = ~((hue >= 180) & (hue <= 265))
    mask = vivid & not_veg & not_water
    lbl, n = ndimage.label(mask)
    objects: List[dict] = []
    keep = np.zeros_like(mask)
    if n > 0:
        sizes = ndimage.sum(np.ones_like(lbl), lbl, index=np.arange(1, n + 1))
        coms = ndimage.center_of_mass(mask, lbl, index=np.arange(1, n + 1))
        for i, (area, (cy, cx)) in enumerate(zip(sizes, coms)):
            if 2 <= area <= 80:  # small isolated props only
                keep[lbl == (i + 1)] = True
                lon, lat = geo.pixel_to_geo(cx, cy, pixel_side)
                east, north = geo.local_enu(cx, cy, pixel_side)
                objects.append({
                    "px": [float(cx), float(cy)],
                    "lon": lon, "lat": lat,
                    "local_e_m": east, "local_n_m": north,
                    "area_px": float(area),
                })
    return keep, objects


def _detect_water(rgb: np.ndarray, min_area: int = 40) -> np.ndarray:
    """Per-pixel water mask: blue-dominant + smooth gradient, morphologically
    cleaned.  Cluster means average water with dark surfaces and miss it, so a
    direct pixel test is far more reliable (see design notes)."""
    r = rgb[..., 0].astype(np.int16)
    g = rgb[..., 1].astype(np.int16)
    b = rgb[..., 2].astype(np.int16)
    mask = (b - r > 25) & (b >= g) & (g >= r)
    mask = ndimage.binary_opening(mask, iterations=1)
    lbl, n = ndimage.label(mask)
    if n:
        sizes = ndimage.sum(np.ones_like(lbl), lbl, index=np.arange(1, n + 1))
        keep_ids = {i + 1 for i, a in enumerate(sizes) if a >= min_area}
        mask = np.isin(lbl, list(keep_ids))
    return mask


def segment(rgb: np.ndarray, geo, n_clusters: int = 12, seed: int = 7,
            soft_sigma: float = 1.5) -> SegResult:
    """Segment `rgb` (HxWx3 uint8) into material classes + soft splat weights."""
    h, w = rgb.shape[:2]
    pixel_side = h  # square tiles

    # --- features: Lab colour + local texture energy --------------------------
    lab = rgb2lab(rgb.astype(np.float32) / 255.0).astype(np.float32)
    gray = lab[..., 0] / 100.0
    mean = ndimage.uniform_filter(gray, size=5)
    sqmean = ndimage.uniform_filter(gray * gray, size=5)
    texture = np.sqrt(np.clip(sqmean - mean * mean, 0, None))  # local std
    feats = np.dstack([
        lab[..., 0] / 100.0,
        (lab[..., 1] + 128.0) / 255.0,
        (lab[..., 2] + 128.0) / 255.0,
        np.clip(texture * 4.0, 0, 1),
    ]).reshape(-1, 4)

    # --- cluster (fit on a subsample for speed, predict all) ------------------
    rng = np.random.default_rng(seed)
    n_px = feats.shape[0]
    sub_idx = rng.choice(n_px, size=min(20000, n_px), replace=False)
    km = KMeans(n_clusters=n_clusters, n_init=4, random_state=seed)
    km.fit(feats[sub_idx])
    cluster_of = km.predict(feats).reshape(h, w)

    # --- map clusters -> classes ---------------------------------------------
    class_of_cluster = np.zeros(n_clusters, dtype=np.int64)
    for k in range(n_clusters):
        m = cluster_of == k
        if not m.any():
            continue
        mean_rgb = rgb[m].reshape(-1, 3).mean(axis=0)
        tex_k = float(texture[m].mean())
        class_of_cluster[k] = _centroid_to_class(mean_rgb, tex_k)
    class_map = class_of_cluster[cluster_of].astype(np.int8)

    # --- per-pixel vegetation split (grass vs tree canopy by brightness) ------
    value = rgb.max(axis=2).astype(np.float32) / 255.0
    veg = (class_map == 0) | (class_map == 1)
    class_map[veg & (value < 0.40)] = 1
    class_map[veg & (value >= 0.40)] = 0

    # --- override with per-pixel water detection ------------------------------
    class_map[_detect_water(rgb)] = 5

    # --- override with detected ground objects --------------------------------
    obj_mask, objects = _detect_objects(rgb, geo, pixel_side)
    class_map[obj_mask] = 7

    # --- soft splat: blur one-hot masks, renormalise --------------------------
    splat = np.zeros((h, w, N_CLASSES), dtype=np.float32)
    for c in range(N_CLASSES):
        splat[..., c] = ndimage.gaussian_filter((class_map == c).astype(np.float32),
                                                sigma=soft_sigma)
    ssum = splat.sum(axis=2, keepdims=True)
    splat = splat / np.clip(ssum, 1e-6, None)

    # --- legend with coverage -------------------------------------------------
    legend = []
    total = float(h * w)
    for c in range(N_CLASSES):
        cov = float((class_map == c).sum()) / total
        legend.append({
            "id": c, "key": CLASS_KEYS[c], "name": CLASS_NAMES[c],
            "color": list(CLASS_COLORS[c]), "coverage": round(cov, 5),
        })
    return SegResult(class_map=class_map, splat=splat, legend=legend, objects=objects)
