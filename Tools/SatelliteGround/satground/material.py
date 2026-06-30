"""
Material stage: turn the class legend into a layered-splat material spec.

The spec is the recipe the UE side rebuilds as a multi-layer landscape/ground
material: each class is one layer whose weight comes from a splat channel, tinted
by the macro albedo and detailed by a tiling PBR material + low height for POM.
`masked_by_osm` flags layers that should yield to existing vector content
(buildings handled by OSM footprints); `scatter` flags classes placed as meshes
(ground objects), not painted.
"""
from __future__ import annotations

from typing import List

# Per-class PBR + authoring hints. roughness/height_scale_m are starting points.
CLASS_PBR = {
    "grass":    {"roughness": 0.92, "height_scale_m": 0.10, "tiling_m": 2.0,
                 "detail_material": "M_Detail_Grass"},
    "tree":     {"roughness": 0.88, "height_scale_m": 0.35, "tiling_m": 4.0,
                 "detail_material": "M_Detail_Canopy", "scatter_hint": True},
    "road":     {"roughness": 0.68, "height_scale_m": 0.015, "tiling_m": 3.0,
                 "detail_material": "M_Detail_Asphalt"},
    "concrete": {"roughness": 0.80, "height_scale_m": 0.02, "tiling_m": 2.5,
                 "detail_material": "M_Detail_Concrete"},
    "building": {"roughness": 0.60, "height_scale_m": 0.0, "tiling_m": 2.0,
                 "detail_material": "M_Detail_Roof", "masked_by_osm": True},
    "water":    {"roughness": 0.08, "height_scale_m": 0.0, "tiling_m": 6.0,
                 "detail_material": "M_Water", "is_water": True},
    "soil":     {"roughness": 0.95, "height_scale_m": 0.08, "tiling_m": 2.0,
                 "detail_material": "M_Detail_Soil"},
    "object":   {"roughness": 0.5, "height_scale_m": 0.4, "tiling_m": 1.0,
                 "detail_material": None, "scatter": True},
}

# Splat textures pack 4 class weights per RGBA image.
CHANNELS = ["R", "G", "B", "A"]


def build_material_spec(legend: List[dict], albedo_name: str, normal_name: str,
                        splat_names: List[str], uv_world_scale_m: float) -> dict:
    """Assemble the layered-splat material spec from the class legend."""
    layers = []
    for entry in legend:
        c = entry["id"]
        key = entry["key"]
        pbr = CLASS_PBR.get(key, {})
        tex_index = c // 4
        ch = CHANNELS[c % 4]
        layers.append({
            "class_id": c,
            "class_key": key,
            "name": entry["name"],
            "base_color": [v / 255.0 for v in entry["color"]],
            "coverage": entry["coverage"],
            "weight_source": {"texture": splat_names[tex_index], "channel": ch},
            "roughness": pbr.get("roughness", 0.8),
            "height_scale_m": pbr.get("height_scale_m", 0.05),
            "tiling_m": pbr.get("tiling_m", 2.0),
            "detail_material": pbr.get("detail_material"),
            "masked_by_osm": pbr.get("masked_by_osm", False),
            "is_water": pbr.get("is_water", False),
            "scatter": pbr.get("scatter", False),
        })
    return {
        "type": "layered_splat",
        "blend": "macro_albedo * detail_tiling, weighted by splat",
        "macro_albedo": albedo_name,
        "macro_normal": normal_name,
        "uv_world_scale_m": uv_world_scale_m,
        "splat_textures": splat_names,
        "splat_packing": "4 class weights per RGBA texture",
        "layers": layers,
    }
