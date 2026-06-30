#!/usr/bin/env python3
"""
End-to-end orchestrator for the satellite-ground prototype.

    satellite tile  ->  super-resolution  ->  segmentation (layers)
                    ->  relief (height/normal)  ->  georeferenced mesh
                    ->  surface_tile.json  +  previews

Run:
    python3 pipeline.py                       # synthetic stand-in tile
    python3 pipeline.py --input input         # uses input/tile.png if present
    python3 pipeline.py --grid-n 160 --sr 2

Outputs land in --output (default: output/).
"""
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import numpy as np
from PIL import Image

from satground import (datastructure, material, mesh, preview, relief, segment,
                       superres, tile_source)


def save_height16(height: np.ndarray, stats: dict, path: Path):
    h16 = relief.height_to_uint16(height, stats)
    Image.fromarray(h16.astype(np.uint16)).save(path)  # PIL mode I;16


def main():
    ap = argparse.ArgumentParser(description="Sphinx satellite-ground prototype")
    ap.add_argument("--input", default="input", help="dir with optional tile.png")
    ap.add_argument("--output", default="output", help="output dir")
    ap.add_argument("--sr", type=int, default=2, help="super-resolution scale")
    ap.add_argument("--grid-n", type=int, default=128, help="mesh grid quads/edge")
    ap.add_argument("--tile-id", default="shanghai_demo_0_0")
    args = ap.parse_args()

    here = Path(__file__).parent
    in_dir = (here / args.input) if not Path(args.input).is_absolute() else Path(args.input)
    out_dir = (here / args.output) if not Path(args.output).is_absolute() else Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    t0 = time.time()
    print("[1/7] loading tile source ...")
    rgb, geo, source, truth = tile_source.load_or_synthesize(in_dir)
    Image.fromarray(rgb).save(out_dir / "source_tile.png")
    print(f"      source={source['kind']} px={rgb.shape[0]} "
          f"anchor=({geo.origin_lon},{geo.origin_lat}) size={geo.size_m}m "
          f"UTM{geo.utm_zone}{'N' if geo.northern else 'S'} epsg={geo.epsg}")

    print(f"[2/7] super-resolution x{args.sr} ...")
    albedo, sr_info = superres.super_resolve(rgb, scale=args.sr)
    Image.fromarray(albedo).save(out_dir / "albedo.png")
    print(f"      albedo={albedo.shape[1]}x{albedo.shape[0]} "
          f"method={sr_info['method']} mpp={geo.mpp(albedo.shape[0]):.3f}")

    print("[3/7] segmentation -> layers ...")
    seg = segment.segment(rgb, geo)
    splat_names = datastructure.write_splat_textures(seg.splat, out_dir)
    for c in seg.legend:
        if c["coverage"] > 0.005:
            print(f"      {c['key']:9s} {c['coverage']*100:5.1f}%")
    print(f"      ground objects detected: {len(seg.objects)}")

    print("[4/7] relief (height + normal) ...")
    height, normal, hstats = relief.build_relief(seg.class_map, seg.splat, geo)
    save_height16(height, hstats, out_dir / "height_r16.png")
    Image.fromarray(normal).save(out_dir / "normal.png")
    print(f"      height range: {hstats['min_m']:.3f}..{hstats['max_m']:.3f} m")

    print(f"[5/7] mesh (grid {args.grid_n}x{args.grid_n}) ...")
    mesh_arrays, mesh_meta = mesh.build_grid_mesh(height, geo, grid_n=args.grid_n)
    mesh.write_obj(mesh_arrays, out_dir / "ground.obj")
    mesh.write_glb(mesh_arrays, out_dir / "ground.glb")
    print(f"      verts={mesh_meta['vertices']} tris={mesh_meta['triangles']}")

    print("[6/7] material spec + data structure ...")
    mat = material.build_material_spec(
        seg.legend, "albedo.png", "normal.png", splat_names, uv_world_scale_m=2.0)
    stats = {
        "duration_s": None,
        "n_objects": len(seg.objects),
        "albedo_px": albedo.shape[0],
        "splat_px": seg.class_map.shape[0],
    }
    if truth is not None:
        acc = float((seg.class_map == truth).mean())
        stats["segmentation_pixel_accuracy_vs_truth"] = round(acc, 4)
        print(f"      segmentation pixel accuracy vs synthetic truth: {acc*100:.1f}%")

    raster_names = {
        "albedo": "albedo.png", "splat": splat_names,
        "height": "height_r16.png", "normal": "normal.png",
        "obj": "ground.obj", "glb": "ground.glb",
    }
    data = datastructure.build_surface_tile(
        args.tile_id, geo, source, sr_info, albedo.shape, seg, hstats,
        mesh_meta, mat, raster_names, stats)

    print("[7/7] previews ...")
    prev = preview.write_previews(out_dir, albedo, seg, height,
                                  geo.mpp(albedo.shape[0]), geo.mpp(seg.class_map.shape[0]))
    data["previews"] = prev

    stats["duration_s"] = round(time.time() - t0, 2)
    data["stats"] = stats
    datastructure.write_json(data, out_dir / "surface_tile.json")

    # --- georeference self-check ---------------------------------------------
    aw = albedo.shape[0]
    tl = geo.pixel_to_geo(0, 0, aw)            # top-left -> NW corner
    br = geo.pixel_to_geo(aw, aw, aw)          # bottom-right -> SE corner
    print(f"      geo check: NW=({tl[0]:.6f},{tl[1]:.6f}) "
          f"SE=({br[0]:.6f},{br[1]:.6f})")
    assert tl[1] > br[1], "north edge must have larger latitude"
    assert br[0] > tl[0], "east edge must have larger longitude"

    print(f"\nDONE in {stats['duration_s']}s -> {out_dir}")
    print(f"  data structure: {out_dir/'surface_tile.json'}")
    print(f"  montage:        {out_dir/'preview_montage.png'}")
    print(f"  3D mesh:        {out_dir/'ground.glb'}")


if __name__ == "__main__":
    main()
