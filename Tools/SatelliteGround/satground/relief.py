"""
Relief stage: turn the class/splat maps into a low-relief height field + a
normal map, so the ground reads as 3D ("立体感") WITHOUT moving high-poly
geometry.  Heights are deliberately small (sub-decimetre for paving/grass, up to
~1 m for canopy/props) -- the "not very tall objects" the user asked for.

Outputs:
  height : float32 (H,W) metres above the tile datum
  normal : uint8   (H,W,3) tangent-space-ish normal map (for material POM/normal)
Both are derived in metric space using the tile's metres-per-pixel so slopes are
physically scaled.
"""
from __future__ import annotations

import numpy as np
from scipy import ndimage

#                 grass tree  road  concr build water soil  object
BASE_HEIGHT_M = [0.06, 0.80, 0.00, 0.03, 0.00, -0.04, 0.05, 0.40]
DETAIL_AMP_M  = [0.10, 0.35, 0.015, 0.02, 0.01, 0.02, 0.08, 0.15]


def _fractal(shape, seed: int, scales=(2, 4, 8, 16, 32)) -> np.ndarray:
    """Multi-scale smoothed white noise in [0,1]."""
    rng = np.random.default_rng(seed)
    h, w = shape
    acc = np.zeros((h, w), dtype=np.float32)
    amp = 1.0
    tot = 0.0
    for s in scales:
        white = rng.standard_normal((h, w)).astype(np.float32)
        acc += amp * ndimage.gaussian_filter(white, sigma=max(h, w) / (s * 4.0))
        tot += amp
        amp *= 0.6
    acc /= max(tot, 1e-6)
    acc -= acc.min()
    acc /= max(acc.max(), 1e-6)
    return acc


def build_relief(class_map: np.ndarray, splat: np.ndarray, geo, seed: int = 7
                 ) -> tuple[np.ndarray, np.ndarray, dict]:
    h, w = class_map.shape
    pixel_side = h
    mpp = geo.mpp(pixel_side)

    base_field = np.zeros((h, w), dtype=np.float32)
    amp_field = np.zeros((h, w), dtype=np.float32)
    for c in range(splat.shape[2]):
        base_field += splat[..., c] * BASE_HEIGHT_M[c]
        amp_field += splat[..., c] * DETAIL_AMP_M[c]

    noise = _fractal((h, w), seed)
    height = base_field + amp_field * (noise - 0.5) * 2.0
    height = ndimage.gaussian_filter(height, sigma=0.8)

    # Normal map from metric gradients: spacing = mpp metres between pixels.
    dzdy, dzdx = np.gradient(height, mpp)
    nx, ny, nz = -dzdx, -dzdy, np.ones_like(height)
    norm = np.sqrt(nx * nx + ny * ny + nz * nz)
    nx, ny, nz = nx / norm, ny / norm, nz / norm
    normal = np.dstack([(nx * 0.5 + 0.5), (ny * 0.5 + 0.5), (nz * 0.5 + 0.5)])
    normal = (np.clip(normal, 0, 1) * 255 + 0.5).astype(np.uint8)

    stats = {
        "min_m": float(height.min()),
        "max_m": float(height.max()),
        "mpp": mpp,
        "encoding": "linear16",
    }
    return height.astype(np.float32), normal, stats


def height_to_uint16(height: np.ndarray, stats: dict) -> np.ndarray:
    lo, hi = stats["min_m"], stats["max_m"]
    rng = max(hi - lo, 1e-6)
    return ((height - lo) / rng * 65535.0 + 0.5).astype(np.uint16)
