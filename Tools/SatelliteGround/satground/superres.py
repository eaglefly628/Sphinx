"""
Super-resolution stage.

Two backends, picked automatically:

* learned  -- OpenCV dnn_superres running real pretrained CNNs
              (EDSR_x4 for quality, FSRCNN_x4 as the fast path).  These are
              trained restoration models, so recovered edges/texture are
              inferred detail, not just interpolation.  Models live in
              `models/*.pb` next to the package; grab them from
              raw.githubusercontent.com/Saafke/{EDSR,FSRCNN}_Tensorflow.
* classical -- Lanczos + edge-aware unsharp; no weights needed, never
              hallucinates.  Used when models/cv2 are absent.

Both preserve the pixel grid, so the geotransform only needs its
metres-per-pixel divided by `scale`.  `info["method"]` records what actually
ran so surface_tile.json stays honest about where detail came from.
"""
from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image
from skimage.filters import unsharp_mask

MODELS_DIR = Path(__file__).resolve().parent.parent / "models"

# EDSR is heavy on CPU: run it on overlapping chunks and blend the seams.
_EDSR_CHUNK = 128
_EDSR_OVERLAP = 12


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


def _load_model(name: str, scale: int):
    from cv2 import dnn_superres
    path = MODELS_DIR / f"{name.upper()}_x{scale}.pb"
    if not path.exists():
        raise FileNotFoundError(path)
    sr = dnn_superres.DnnSuperResImpl_create()
    sr.readModel(str(path))
    sr.setModel(name.lower(), scale)
    return sr


def _upsample_tiled(sr, rgb: np.ndarray, scale: int,
                    chunk: int = _EDSR_CHUNK, overlap: int = _EDSR_OVERLAP
                    ) -> np.ndarray:
    """Chunked inference with overlap cropping so seams don't show."""
    h, w = rgb.shape[:2]
    out = np.zeros((h * scale, w * scale, 3), dtype=np.uint8)
    step = chunk - 2 * overlap
    for y0 in range(0, h, step):
        for x0 in range(0, w, step):
            ya, xa = max(0, y0 - overlap), max(0, x0 - overlap)
            yb, xb = min(h, y0 + step + overlap), min(w, x0 + step + overlap)
            up = sr.upsample(rgb[ya:yb, xa:xb])
            # crop the model output back to this chunk's core region
            cy, cx = (y0 - ya) * scale, (x0 - xa) * scale
            ch = (min(h, y0 + step) - y0) * scale
            cw = (min(w, x0 + step) - x0) * scale
            out[y0 * scale:y0 * scale + ch,
                x0 * scale:x0 * scale + cw] = up[cy:cy + ch, cx:cx + cw]
    return out


def _learned(rgb: np.ndarray, scale: int, quality: str) -> tuple[np.ndarray, dict]:
    """Run a real SR network.  Models are x4; other scales resample after."""
    native = 4
    if quality == "high":
        sr = _load_model("edsr", native)
        up = _upsample_tiled(sr, rgb, native)
        method = "edsr_x4_cnn"
    else:
        sr = _load_model("fsrcnn", native)
        up = sr.upsample(rgb)
        method = "fsrcnn_x4_cnn"
    if scale != native:
        h, w = rgb.shape[:2]
        up = np.asarray(Image.fromarray(up).resize(
            (w * scale, h * scale), Image.LANCZOS))
        method += f"_resampled_x{scale}"
    return up, {"method": method, "scale": scale, "hallucinated": True,
                "model": f"{'EDSR' if quality == 'high' else 'FSRCNN'}_x4"}


def super_resolve(rgb: np.ndarray, scale: int = 2, method: str = "auto",
                  quality: str = "high") -> tuple[np.ndarray, dict]:
    """Return (upsampled_rgb, info).

    method: "auto" tries the learned backend and falls back to classical;
            "learned" / "classical" force a path (learned raises if absent).
    quality: "high" = EDSR (slow, CPU-tiled), "fast" = FSRCNN.
    """
    if method in ("auto", "learned"):
        try:
            return _learned(rgb, scale, quality)
        except Exception as exc:
            if method == "learned":
                raise
            out = _classical(rgb, scale)
            return out, {"method": "classical_fallback", "scale": scale,
                         "hallucinated": False,
                         "reason": f"learned backend unavailable: {exc}"}
    out = _classical(rgb, scale)
    return out, {"method": "classical_lanczos_unsharp", "scale": scale,
                 "hallucinated": False}
