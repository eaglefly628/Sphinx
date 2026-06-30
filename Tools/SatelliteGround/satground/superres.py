"""
Super-resolution stage.

Goal here is NOT to hallucinate ground truth -- it is to upsample the macro
albedo so mid-range views are not blurry, while the real per-class detail comes
later from tiling PBR materials.  So the default is a robust classical path
(Lanczos upsample + edge-aware detail enhancement) that needs no GPU/weights.

A learned backend (e.g. Real-ESRGAN ONNX) can be slotted in via `method="ai"`
once weights/GPU are available; it must preserve the pixel grid so the
geotransform only needs its metres-per-pixel divided by `scale`.
"""
from __future__ import annotations

import numpy as np
from PIL import Image
from skimage.filters import unsharp_mask


def _classical(rgb: np.ndarray, scale: int) -> np.ndarray:
    h, w = rgb.shape[:2]
    up = Image.fromarray(rgb).resize((w * scale, h * scale), Image.LANCZOS)
    arr = np.asarray(up, dtype=np.float32) / 255.0
    # Edge-aware detail boost per channel; radius scales with upsample factor.
    out = np.zeros_like(arr)
    for c in range(3):
        out[..., c] = unsharp_mask(arr[..., c], radius=1.2 * scale,
                                   amount=0.8, preserve_range=True)
    out = np.clip(out, 0.0, 1.0)
    return (out * 255.0 + 0.5).astype(np.uint8)


def super_resolve(rgb: np.ndarray, scale: int = 2, method: str = "classical"
                  ) -> tuple[np.ndarray, dict]:
    """Return (upsampled_rgb, info).

    `info` records the method actually used so the data structure can be honest
    about whether detail is real or interpolated.
    """
    if method == "ai":
        try:
            return _ai_backend(rgb, scale)
        except Exception as exc:  # pragma: no cover - depends on optional deps
            out = _classical(rgb, scale)
            return out, {"method": "classical_fallback", "scale": scale,
                         "reason": f"ai backend unavailable: {exc}"}
    out = _classical(rgb, scale)
    return out, {"method": "classical_lanczos_unsharp", "scale": scale,
                 "hallucinated": False}


def _ai_backend(rgb: np.ndarray, scale: int):  # pragma: no cover
    """Placeholder for a learned SR backend (Real-ESRGAN ONNX, etc.).

    Intentionally raises until wired up, so callers fall back to classical.
    """
    raise NotImplementedError("AI super-resolution backend not configured")
