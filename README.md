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
| LandUseClassifier | 100% | 9-rule classification + PCG parameter assignment |
| LocalFileProvider | 100% | Local GeoJSON + DEM with caching |
| ArcGISRestProvider | 100% | HTTP queries, pagination, multi-layer |
| PCGGISNode | 100% | Grid sampling with point-in-polygon, 4 metadata attributes |
| GISWorldBuilder | 100% | 3-mode orchestrator, editor preview, async generation |
| LandUseMapDataAsset | 100% | Persistent polygon storage with spatial queries |
| GISCoordinate | 100% | Geo ↔ UE5 world coordinate conversion |
| GISPolygonComponent | 100% | Actor component with debug visualization |

See [`Plugins/GISProcedural/README.md`](Plugins/GISProcedural/README.md) for detailed algorithm documentation.

## Tech Stack

- **Engine**: Unreal Engine 5
- **PCG**: UE5 Procedural Content Generation Framework
- **Data Formats**: GeoJSON, GeoTIFF, PNG Heightmap (Mapbox Terrain RGB), RAW (.r16/.r32), SRTM HGT
- **Online Services**: ArcGIS REST Feature Services
- **Language**: C++ (UE5 plugin), no external dependencies beyond UE5

## License

Internal project — MilSim Team.
