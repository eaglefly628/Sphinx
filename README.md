# Sphinx

> **[中文版说明 →](README_CN.md)**

Unreal Engine 5 project for procedural military simulation world generation driven by real-world GIS data.

## Overview

Sphinx transforms real geographic data — OpenStreetMap exports, DEM elevation tiles, and ArcGIS Feature Services — into classified land-use polygons that drive UE5's Procedural Content Generation (PCG) framework to automatically populate terrain with buildings, vegetation, roads, and water features.

### Key Capabilities

- **Multi-source GIS ingestion** — Local GeoJSON/DEM files or live ArcGIS REST queries
- **Terrain analysis** — DEM → slope/aspect → terrain classification → connected zone extraction
- **Polygon derivation** — Terrain zones cut by road/river/coastline vectors into land-use polygons
- **Automatic classification** — 9-rule system: Residential, Commercial, Industrial, Forest, Farmland, Water, Road, OpenSpace, Military
- **PCG integration** — Custom sampler node outputs points with density/type metadata for downstream spawners
- **Offline → Online pipeline** — Editor-time generation persisted as DataAsset, runtime PCG reads from asset

## Architecture

```
Mode A/B/C: Single-Region Pipeline
───────────────────────────────────
GeoJSON (OSM) ──┐
                ├→ IGISDataProvider → PolygonDeriver → ULandUseMapDataAsset → PCG Sampler
ArcGIS REST   ──┘         │                │            (.uasset)                │
                          │                │                                     │
DEM tiles ────────→ QueryElevation   TerrainAnalyzer                     Attribute Filter
(GeoTIFF/PNG/RAW)         │                │                                     │
                          │          ┌─ With DEM → vector cut                   ├→ Building HISM
                          │          └─ No DEM  → vector polygons               ├→ Tree HISM
                          │                │                                     ├→ Road Spline
                          │          LandUseClassifier                           └→ Water Mesh
                          │                │
                          └──────→ TArray<FLandUsePolygon>

Mode D: Tiled Pipeline (Global Scale)
──────────────────────────────────────
Python Preprocessing (PBF/DEM/WorldCover)
        │
        ├→ tile_manifest.json
        ├→ tiles/tile_X_Y.geojson
        ├→ tiles/tile_X_Y_dem.png
        └→ tiles/tile_X_Y_landcover.json
                │
    TiledFileProvider (LRU cache, dedup)
                │
    TiledWorldBuilder (Editor batch tool)
                │
    Per-tile ULandUseMapDataAsset (spatial index)
                │
    TiledLandUseMapDataAsset (async streaming catalog)
                │
        PCG Sampler (tile-aware, LoadRadius)
```

## Quick Start

### Local File Mode

1. Export region data from [overpass-turbo](https://overpass-turbo.eu/) as GeoJSON
2. (Optional) Download DEM tiles from [OpenTopography](https://opentopography.org/) or SRTM
3. Place files under `Content/GISData/YourRegion/`
4. Drop `AGISWorldBuilder` into your level
5. Set `DataSourceType = LocalFile`, configure paths and origin coordinates
6. **Generate In Editor** → preview → **Generate And Save Data Asset**
7. Wire the DataAsset into your PCG graph via the GIS Land Use Sampler node

### ArcGIS REST Mode

1. Set `DataSourceType = ArcGISRest`
2. Enter Feature Service URL and API key
3. **Generate And Save Data Asset** → same downstream PCG workflow

### Tiled File Mode (Global Scale)

1. Prepare raw data: OSM PBF, DEM GeoTIFF, ESA WorldCover GeoTIFF
2. Run the Python preprocessing pipeline:
   ```bash
   python Tools/GISPreprocess/preprocess.py --input ./RawData --output Content/GISData/Region_01 --tile-size 1024
   ```
3. Set `DataSourceType = TiledFile`, point `TileManifestPath` to the generated `tile_manifest.json`
4. Use `ATiledWorldBuilder` in editor to batch-generate per-tile DataAssets with land cover fusion
5. Reference the `UTiledLandUseMapDataAsset` catalog in your PCG graph — tiles stream automatically

## Plugin: GISProcedural

| Module | Status | Purpose |
|--------|--------|---------|
| GeoJsonParser | 100% | Parse GeoJSON with OSM category inference |
| DEMParser | 85% | GeoTIFF (simplified), PNG heightmap, RAW, SRTM HGT |
| TerrainAnalyzer | 100% | Elevation grid → slope → classify → CCL → boundary polygons |
| PolygonDeriver | 100% | Terrain zone + vector cutting (Sutherland-Hodgman variant) |
| RoadNetworkGraph | 100% | Planar graph with intersection detection and edge splitting |
| LandUseClassifier | 100% | 9-rule classification + PCG param assignment + LandCover fusion |
| LocalFileProvider | 100% | Local GeoJSON + DEM with caching |
| ArcGISRestProvider | 100% | HTTP queries, pagination, multi-layer |
| PCGGISNode | 100% | Grid sampling with point-in-polygon, tiling-ready config |
| GISWorldBuilder | 100% | 4-mode orchestrator (LocalFile/ArcGIS/DataAsset/TiledFile) |
| LandUseMapDataAsset | 100% | Persistent polygon storage with spatial queries |
| GISCoordinate | 100% | Geo ↔ UE5 world + WGS84 ↔ UTM conversion |
| GISPolygonComponent | 100% | Actor component with debug visualization |
| **TileManifest** | 100% | Tile manifest JSON parser, geo-bounds queries |
| **LandCoverGrid** | 100% | ESA WorldCover raster types for land cover fusion |
| **TiledFileProvider** | 100% | Tile-based data provider with LRU cache, cross-tile dedup |
| **RasterLandCoverParser** | 100% | Parse landcover.json, majority-class region queries |
| **TiledLandUseMapDataAsset** | 100% | Per-tile DataAsset catalog, async streaming via FStreamableManager |
| **TiledWorldBuilder** | 100% | Editor batch tool: manifest → per-tile DataAsset generation |

See [`Plugins/GISProcedural/README.md`](Plugins/GISProcedural/README.md) for detailed algorithm documentation.

## Scaling Roadmap (Global Military Simulation)

All 4 phases are implemented. The plugin scales from ~10 km² demos to global coverage:

| Phase | Scope | Status |
|-------|-------|--------|
| **P0** Interface Contracts | Shared types, UTM projection, LandCover fusion API | Done |
| **P1** External Preprocessing | Python/GDAL pipeline: PBF→GeoJSON, DEM tiling, WorldCover | Done |
| **P2** TiledFileProvider + Spatial Index | Tile manifest parser, grid spatial index, LRU cache | Done |
| **P3** Tiled DataAsset + Streaming | Per-tile DataAsset generation, async streaming catalog | Done |

See [`PLAN.md`](PLAN.md) for the original collaboration plan.

## Development Workflow

```
1. Prepare GIS data
   overpass-turbo → GeoJSON export
   OpenTopography / SRTM → DEM tiles
   ESA WorldCover → LandCover GeoTIFF (optional, 10m resolution)

2. Run preprocessing pipeline (for tiled mode)
   python Tools/GISPreprocess/preprocess.py --input ./RawData --output Content/GISData/Region_01 --tile-size 1024

3. Editor: Generate & Preview
   Drop AGISWorldBuilder → Set DataSourceType → GenerateInEditor → visual check
   (or ATiledWorldBuilder for tiled batch generation)

4. Editor: Bake to DataAsset
   GenerateAndSaveDataAsset → ULandUseMapDataAsset persisted under /Game/GISData/
   (Tiled mode: TiledWorldBuilder generates per-tile DataAssets automatically)

5. PCG Graph
   GIS Land Use Sampler node → Attribute Filter → Building/Tree/Road spawners
   (Enable bEnableTiling + set LoadRadius for tile-aware sampling)

6. Runtime Streaming
   TiledLandUseMapDataAsset async-loads tile DataAssets on demand
```

## Tech Stack

- **Engine**: Unreal Engine 5
- **PCG**: UE5 Procedural Content Generation Framework
- **Data Formats**: GeoJSON, GeoTIFF, PNG Heightmap (Mapbox Terrain RGB), RAW (.r16/.r32), SRTM HGT
- **Online Services**: ArcGIS REST Feature Services
- **Preprocessing**: Python 3.10+, GDAL/OGR, osmium-tool (Phase 1+)
- **Projection**: WGS84 ↔ UTM (built-in), Simplified Mercator for local regions
- **Language**: C++ (UE5 plugin), Python (external preprocessing)

## License

Internal project — MilSim Team.
