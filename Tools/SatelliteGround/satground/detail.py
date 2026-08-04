"""
Class-guided micro-detail synthesis.

Even a real SR network cannot recover texture below the sensor's ground
sample distance, so this stage restores *plausible* sub-pixel material
character: each class gets a procedural micro-texture with physically sized
features (asphalt grain ~5 cm, grass clumps ~35 cm, ...), and the fields are
blended into the albedo weighted by the splat maps.  Deterministic (seeded),
grid-preserving, and honest: surface_tile.json records it as synthesized.

Order matches segment.CLASS_KEYS:
    grass, tree, road, concrete, building, water, soil, object
"""
from __future__ import annotations

import numpy as np
from scipy.ndimage import gaussian_filter, zoom

# Per-class (feature_size_m, luminance_amplitude).  Amplitude is the max
# +/- relative luminance modulation contributed at full class weight.
CLASS_DETAIL = [
    (0.35, 0.10),   # grass: clumpy tufts
    (0.60, 0.12),   # tree: canopy leaf-cluster lumps
    (0.06, 0.05),   # road: fine asphalt grain
    (0.10, 0.05),   # concrete: speckle
    (0.25, 0.07),   # building roof: gravel/tile granularity
    (1.50, 0.04),   # water: broad ripple shading
    (0.20, 0.09),   # soil: mottling
    (0.15, 0.06),   # object: generic grain
]
# Slight per-class chroma jitter (applied to G channel for vegetation,
# uniformly elsewhere) so detail is not purely luminance.
CLASS_CHROMA = [0.05, 0.06, 0.0, 0.0, 0.02, 0.0, 0.04, 0.0]


def _noise(shape: tuple[int, int], feature_px: float, rng) -> np.ndarray:
    """Band-limited value noise with ~feature_px feature size, in [-1, 1]."""
    feature_px = max(feature_px, 1.0)
    h, w = shape
    gh = max(2, int(np.ceil(h / feature_px)) + 1)
    gw = max(2, int(np.ceil(w / feature_px)) + 1)
    coarse = rng.random((gh, gw), dtype=np.float32)
    field = zoom(coarse, (h / gh, w / gw), order=3, mode="nearest",
                 grid_mode=True)[:h, :w]
    field = gaussian_filter(field, sigma=feature_px * 0.15)
    field -= field.mean()
    peak = max(float(np.abs(field).max()), 1e-6)
    return (field / peak).astype(np.float32)


def _upsample_splat(splat: np.ndarray, out_hw: tuple[int, int]) -> np.ndarray:
    h, w = out_hw
    sh, sw, c = splat.shape
    if (sh, sw) == (h, w):
        return splat
    up = zoom(splat, (h / sh, w / sw, 1), order=1, mode="nearest",
              grid_mode=True)[:h, :w]
    up = np.clip(up, 0.0, 1.0)
    s = up.sum(axis=2, keepdims=True)
    return (up / np.maximum(s, 1e-6)).astype(np.float32)


def enrich_albedo(albedo: np.ndarray, splat: np.ndarray, mpp: float,
                  seed: int = 7, strength: float = 1.0
                  ) -> tuple[np.ndarray, dict]:
    """Blend per-class micro-textures into `albedo` (uint8 HxWx3).

    splat may be at a coarser resolution; it is upsampled and renormalised.
    mpp is metres-per-pixel of `albedo`, used to size features physically.
    Returns (enriched_uint8, info).
    """
    h, w = albedo.shape[:2]
    wts = _upsample_splat(splat.astype(np.float32), (h, w))
    rng = np.random.default_rng(seed)

    lum = np.zeros((h, w), dtype=np.float32)
    green = np.zeros((h, w), dtype=np.float32)
    for c, (size_m, amp) in enumerate(CLASS_DETAIL):
        n = _noise((h, w), size_m / mpp, rng)
        lum += wts[..., c] * amp * n
        if CLASS_CHROMA[c] > 0.0:
            n2 = _noise((h, w), (size_m * 2.0) / mpp, rng)
            green += wts[..., c] * CLASS_CHROMA[c] * n2

    # Directional streak grain on roads (tyre polish / lane wear).
    road = wts[..., 2]
    if float(road.max()) > 0.05:
        streak = _noise((h, w), 0.08 / mpp, rng)
        streak = gaussian_filter(streak, sigma=(0.5, 3.0 / max(mpp, 1e-3) * 0.2 + 1.5))
        peak = max(float(np.abs(streak).max()), 1e-6)
        lum += road * 0.03 * (streak / peak)

    arr = albedo.astype(np.float32) / 255.0
    mod = 1.0 + strength * lum[..., None]
    out = arr * mod
    out[..., 1] *= 1.0 + strength * green
    out = np.clip(out, 0.0, 1.0)
    return ((out * 255.0) + 0.5).astype(np.uint8), {
        "applied": True, "seed": seed, "strength": strength,
        "classes": [{"key_index": i, "feature_m": s, "amplitude": a}
                    for i, (s, a) in enumerate(CLASS_DETAIL)],
        "note": "procedural micro-texture, synthesized (not sensed)",
    }
