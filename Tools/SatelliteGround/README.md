# Satellite Ground Surface — prototype

Turn a satellite tile into a **georeferenced, layered ground-surface data
structure** that can later be imported into UE5 to add ground richness the vector
pipeline (buildings / roads / water) cannot express: surface colour, material
classes, low relief ("立体感"), and small ground props.

This is a **standalone demo**. It does not depend on UE; it produces an
engine-agnostic `surface_tile.json` + raster/mesh assets, and a browser viewer so
the result is inspectable immediately.

```
satellite tile ─▶ super-resolution ─▶ segmentation (layers) ─▶ relief (height/normal)
              ─▶ georeferenced ground mesh ─▶ surface_tile.json + previews + 3D viewer
```

## Why this design

- **Segmentation is the lever, not super-resolution.** The photo gives macro
  layout + colour; *which tiling PBR material* each pixel uses comes from
  segmentation. So SR stays a modest classical upscale (no hallucinated ground
  truth), and the rich close-up detail / relief comes from per-class materials.
- **Georeferenced by construction.** Every product carries a geo-anchor (SW
  corner WGS84 + UTM geotransform), mirroring `Tools/GISPreprocess/projection.py`
  and the C++ `GISCoordinate` / `FTileManifest`, so it drops onto the Cesium globe
  aligned with existing vector content.
- **Relief without high-poly geometry.** Height stays sub-decimetre for
  paving/grass and up to ~1 m for canopy/props — driven by a height + normal map
  for POM in the material, plus a gently displaced grid mesh.

## Install & run

```bash
pip install -r requirements.txt
python3 pipeline.py                 # uses synthetic stand-in tile
python3 pipeline.py --input input   # uses input/tile.png if present
```

To use a **real tile**, drop `input/tile.png` (square) and optionally
`input/tile.json` = `{"origin_lon":..,"origin_lat":..,"size_m":..}` (SW corner +
edge length in metres). The pipeline is otherwise identical.

> NOTE: in the sandboxed remote environment the egress proxy blocks satellite
> tile servers (arcgisonline / google / osm → HTTP 403), so a procedurally
> synthesised city tile is used as a stand-in. It is **not** real imagery; swap
> in a real tile via `input/`.

## View the result (no UE needed)

```bash
python3 -m http.server 8765        # from this directory
# open http://127.0.0.1:8765/viewer/index.html
```

The viewer (three.js, vendored under `viewer/vendor/`, offline) renders the
georeferenced mesh with the layered-splat material. Toggle **Satellite /
Layered / Segmentation**, adjust **relief exaggeration** and **sun azimuth**,
and show detected **objects**. Zero-dependency static previews are written to
`output/preview_*.png`.

`viewer/shot.mjs` is a dev helper that renders the viewer headlessly with the
pre-installed Chromium and saves a screenshot (used to validate WebGL output).

## Output: `output/surface_tile.json` (schema `sphinx.surface_tile/1.0`)

| field | meaning |
|-------|---------|
| `geo` | WGS84 SW anchor, UTM zone/epsg, geo bbox, `geotransform_utm/geo` (pixel→CRS) |
| `super_resolution` | method + whether detail is interpolated (honest, never hallucinated) |
| `raster.albedo` | super-res macro colour texture (+ m/px) |
| `raster.splat` | RGBA-packed per-class weight maps (4 classes/texture) + channel order |
| `raster.height` / `raster.normal` | 16-bit height (linear, min/max in m) + normal map |
| `classes` | id / key / name / display colour / coverage per class |
| `objects` | detected small ground props with lon/lat + local ENU metres (→ PCG scatter) |
| `mesh` | grid mesh meta (ENU-centred frame, geo anchor, GLB/OBJ refs) |
| `material` | layered-splat recipe: per-class base colour, roughness, tiling, detail material, masks |
| `ue_import` | recommended UE placement (GISCoordinate + layered material / RVT) |

## How it maps to the UE side (next step)

- Rebuild the surface procedurally from `geo` + height (via `GISCoordinate`),
  or drape over Cesium terrain; paint the layered material from the splat maps.
- `classes` / `material.layers` → a multi-layer landscape/ground material; layers
  with `masked_by_osm` (buildings) yield to existing OSM footprints.
- `objects[]` → spawn as HISM through the existing PCG pipeline.
- The high-res class map can also upgrade `LandUseClassifier.FuseLandCoverData()`
  (currently fed by 10 m ESA WorldCover) to sub-metre.

## Module layout

```
pipeline.py            orchestrator (CLI)
satground/
  geo.py               WGS84↔UTM, TileGeo anchor + geotransforms
  tile_source.py       load real tile / synthesise stand-in
  superres.py          classical SR (pluggable AI backend)
  segment.py           KMeans + heuristic class mapping, water/object detection, splat
  relief.py            per-class height field + normal map
  mesh.py              georeferenced grid mesh → OBJ + GLB
  material.py          layered-splat material spec
  datastructure.py     assemble + write surface_tile.json, splat textures
  preview.py           static preview PNGs
viewer/                standalone three.js viewer (offline, vendored)
```

## Current limitations / roadmap

- Input is a synthetic stand-in (egress blocks live tiles) — swap real tile in.
- SR is classical; a Real-ESRGAN ONNX backend can slot into `superres._ai_backend`.
- Segmentation is unsupervised heuristics; replace `segment.segment()` with a
  learned remote-sensing model (OpenEarthMap / LoveDA) for production accuracy.
- Single tile; tiling across `tile_manifest.json` + a UE importer come next.
