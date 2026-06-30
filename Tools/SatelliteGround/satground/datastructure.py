"""
Assemble the canonical "surface tile" data structure (surface_tile.json) plus its
referenced raster assets.  This JSON is the contract handed to the UE import step
later; it is intentionally engine-agnostic and georeferenced.

Schema id: sphinx.surface_tile/1.0
"""
from __future__ import annotations

import json
import math
from pathlib import Path
from typing import List

import numpy as np
from PIL import Image

SCHEMA_ID = "sphinx.surface_tile/1.0"


def write_splat_textures(splat: np.ndarray, out_dir: Path) -> List[str]:
    """Pack N class weights into ceil(N/4) RGBA PNGs. Returns filenames."""
    h, w, n = splat.shape
    names = []
    n_tex = math.ceil(n / 4)
    for t in range(n_tex):
        block = np.zeros((h, w, 4), dtype=np.float32)
        for k in range(4):
            c = t * 4 + k
            if c < n:
                block[..., k] = splat[..., c]
        img = (np.clip(block, 0, 1) * 255 + 0.5).astype(np.uint8)
        name = f"splat_{t}_rgba.png"
        Image.fromarray(img, mode="RGBA").save(out_dir / name)
        names.append(name)
    return names


def build_surface_tile(tile_id: str, geo, source: dict, sr_info: dict,
                       albedo_shape, seg, height_stats: dict, mesh_meta: dict,
                       material_spec: dict, raster_names: dict,
                       stats: dict) -> dict:
    ah, aw = albedo_shape[:2]
    sh, sw = seg.class_map.shape
    return {
        "schema": SCHEMA_ID,
        "tile_id": tile_id,
        "source": source,
        "super_resolution": sr_info,
        "geo": {
            **geo.as_dict(),
            "geotransform_utm": geo.geotransform_utm(aw),
            "geotransform_geo": geo.geotransform_geo(aw),
            "note": "geotransform maps albedo pixel(col,row, top-left) -> CRS",
        },
        "raster": {
            "albedo": {"path": raster_names["albedo"], "width": aw, "height": ah,
                       "mpp": geo.mpp(aw)},
            "splat": {"paths": raster_names["splat"], "width": sw, "height": sh,
                      "mpp": geo.mpp(sw),
                      "channels": [c["key"] for c in seg.legend],
                      "packing": "4 weights per RGBA texture"},
            "height": {"path": raster_names["height"], "width": sw, "height": sh,
                       **height_stats},
            "normal": {"path": raster_names["normal"], "width": sw, "height": sh,
                       "space": "tangent-ish, from height gradient"},
        },
        "classes": seg.legend,
        "objects": {
            "count": len(seg.objects),
            "note": "Small ground props -> scatter as 3D meshes via PCG, not painted.",
            "items": seg.objects[:500],
        },
        "mesh": {**mesh_meta, "obj": raster_names["obj"], "glb": raster_names["glb"]},
        "material": material_spec,
        "stats": stats,
        "ue_import": {
            "recommended": "georeferenced ground mesh + layered-splat material",
            "placement": ("SetOrigin(origin_lon, origin_lat) then GeoToWorld; "
                          "rebuild grid from height field; paint layers from splat"),
            "alt": "Runtime Virtual Texture for very large extents",
            "objects": "spawn objects[] as HISM via existing PCG pipeline",
        },
    }


def write_json(data: dict, path: Path):
    Path(path).write_text(json.dumps(data, indent=2, ensure_ascii=False))
