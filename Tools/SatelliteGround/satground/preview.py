"""
Static previews so the result is viewable with zero dependencies (just open the
PNGs).  Complements the interactive 3D viewer.
"""
from __future__ import annotations

from pathlib import Path
from typing import List, Tuple

import numpy as np
from PIL import Image, ImageDraw

from .segment import CLASS_COLORS


def colorize_classes(class_map: np.ndarray) -> np.ndarray:
    out = np.zeros((*class_map.shape, 3), dtype=np.uint8)
    for c, col in enumerate(CLASS_COLORS):
        out[class_map == c] = col
    return out


def hillshade(height: np.ndarray, mpp: float, az_deg=315.0, alt_deg=45.0) -> np.ndarray:
    dzdy, dzdx = np.gradient(height, mpp)
    slope = np.arctan(np.sqrt(dzdx ** 2 + dzdy ** 2))
    aspect = np.arctan2(-dzdy, dzdx)
    az = np.radians(360.0 - az_deg + 90.0)
    alt = np.radians(alt_deg)
    hs = (np.sin(alt) * np.cos(slope) +
          np.cos(alt) * np.sin(slope) * np.cos(az - aspect))
    return np.clip(hs, 0, 1).astype(np.float32)


def relief_render(albedo: np.ndarray, height: np.ndarray, mpp: float) -> np.ndarray:
    """Albedo modulated by exaggerated hillshade -> shows 3D relief in 2D."""
    # Exaggerate height for a legible shaded preview.
    hs = hillshade(height * 6.0, mpp)
    shade = (0.55 + 0.9 * hs)[..., None]
    out = np.clip(albedo.astype(np.float32) * shade, 0, 255).astype(np.uint8)
    return out


def overlay_objects(img: np.ndarray, objects: List[dict], px_side: int,
                    src_side: int) -> np.ndarray:
    out = Image.fromarray(img.copy())
    d = ImageDraw.Draw(out)
    scale = px_side / src_side
    for o in objects:
        x, y = o["px"][0] * scale, o["px"][1] * scale
        r = 4
        d.ellipse([x - r, y - r, x + r, y + r], outline=(255, 0, 255), width=2)
    return np.array(out)


def _label(img: Image.Image, text: str) -> Image.Image:
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, len(text) * 8 + 10, 20], fill=(0, 0, 0))
    d.text((5, 4), text, fill=(255, 255, 255))
    return img


def montage(panels: List[Tuple[str, np.ndarray]], cols: int, cell: int,
            out_path: Path):
    rows = (len(panels) + cols - 1) // cols
    canvas = Image.new("RGB", (cols * cell, rows * cell), (20, 20, 20))
    for i, (title, arr) in enumerate(panels):
        im = Image.fromarray(arr).convert("RGB").resize((cell, cell), Image.LANCZOS)
        im = _label(im, title)
        r, c = divmod(i, cols)
        canvas.paste(im, (c * cell, r * cell))
    canvas.save(out_path)


def write_previews(out_dir: Path, albedo: np.ndarray, seg, height: np.ndarray,
                   mpp_albedo: float, mpp_seg: float) -> dict:
    out_dir = Path(out_dir)
    class_rgb = colorize_classes(seg.class_map)
    relief = relief_render(albedo, _resize_to(height, albedo.shape[:2]),
                           mpp_albedo)
    relief = overlay_objects(relief, seg.objects, albedo.shape[0],
                             seg.class_map.shape[0])

    Image.fromarray(albedo).save(out_dir / "preview_albedo.png")
    Image.fromarray(class_rgb).save(out_dir / "preview_segmentation.png")
    Image.fromarray(relief).save(out_dir / "preview_relief.png")

    panels = [
        ("1. albedo (super-res)", albedo),
        ("2. segmentation", class_rgb),
        ("3. relief + objects", relief),
    ]
    montage(panels, cols=3, cell=420, out_path=out_dir / "preview_montage.png")
    return {
        "albedo": "preview_albedo.png",
        "segmentation": "preview_segmentation.png",
        "relief": "preview_relief.png",
        "montage": "preview_montage.png",
    }


def _resize_to(arr: np.ndarray, shape) -> np.ndarray:
    if arr.shape[:2] == tuple(shape):
        return arr
    im = Image.fromarray(arr.astype(np.float32), mode="F").resize(
        (shape[1], shape[0]), Image.BILINEAR)
    return np.array(im, dtype=np.float32)
