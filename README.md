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
Data Sources              Editor (Offline)              Persistence           Runtime (Online)
─────────────            ─────────────────             ───────────           ────────────────

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
| **TileManifest** | **NEW** | Shared tile metadata types for streaming pipeline |
| **LandCoverGrid** | **NEW** | ESA WorldCover raster types for land cover fusion |

See [`Plugins/GISProcedural/README.md`](Plugins/GISProcedural/README.md) for detailed algorithm documentation.

## Scaling Roadmap (Global Military Simulation)

The plugin follows a 4-phase plan to scale from ~10 km² demos to global coverage:

| Phase | Scope | Owner |
|-------|-------|-------|
| **P0** Interface Contracts | Shared types, UTM projection, LandCover fusion API | 鹰飞 (existing files) |
| **P1** External Preprocessing | Python/GDAL pipeline: PBF→GeoJSON, DEM tiling, WorldCover | 小城 (new files) |
| **P2** TiledFileProvider + Spatial Index | Tile manifest parser, R-tree spatial index, LRU cache | 小城 (new files) |
| **P3** World Partition Integration | WP streaming source, level-instance tile actors | 鹰飞 (existing files) |

See [`PLAN.md`](PLAN.md) for the full collaboration plan.

## Development Workflow

```
1. Prepare GIS data
   overpass-turbo → GeoJSON export
   OpenTopography / SRTM → DEM tiles
   ESA WorldCover → LandCover GeoTIFF (optional, 10m resolution)

2. (P1+) Run preprocessing pipeline
   python Tools/preprocess.py --input ./RawData --output Content/GISData/Region_01 --tile-size 1024

3. Editor: Generate & Preview
   Drop AGISWorldBuilder → Set DataSourceType → GenerateInEditor → visual check

4. Editor: Bake to DataAsset
   GenerateAndSaveDataAsset → ULandUseMapDataAsset persisted under /Game/GISData/

5. PCG Graph
   GIS Land Use Sampler node → Attribute Filter → Building/Tree/Road spawners

6. (P3+) Runtime Streaming
   World Partition auto-loads tile DataAssets as player moves
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
