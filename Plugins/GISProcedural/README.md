# GISProcedural Plugin

Unreal Engine 5 plugin for procedural world generation from real-world GIS data (GeoJSON, DEM, ArcGIS REST, Cesium 3D Tiles).

## Architecture

```
Data Sources              Offline Processing           Persistence          Runtime
─────────────            ──────────────────           ───────────          ───────

Mode A/B/C: Single-Region Pipeline
───────────────────────────────────
GeoJSON files ──┐
                ├→ IGISDataProvider → PolygonDeriver → DataAsset  → PCG Node → Sample Points
ArcGIS REST  ──┘         │                  │         (.uasset)       │
                         │                  │                         │
(Optional) DEM ────→ QueryElevation    TerrainAnalyzer          Attribute Filter
                         │                  │                         │
                         │            ┌─ With DEM: vector cutting    ├─ Building HISM
                         │            └─ No DEM: vector polygons     ├─ Tree HISM
                         │                  │                        ├─ Road Spline
                         │            LandUseClassifier              └─ Water Mesh
                         │                  │
                         │            AssignPCGParams
                         │                  │
                         └────────→ TArray<FLandUsePolygon>

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

Mode E: CesiumTiled (Global Scale + 3D Earth)
──────────────────────────────────────────────
Python Preprocessing (SRTM → elevation cache)
        │
        ├→ dem_cache/elevation_X_Y.bin   ← O(1) bilinear lookup
        ├→ tile_manifest.json            ← vector data index
        └→ tiles/tile_X_Y.geojson        ← local GeoJSON
                │
    CesiumBridgeComponent
        ├→ LLH ↔ UE5 coordinate bridging (ECEF via Cesium or Mercator fallback)
        ├→ Offline DEM elevation cache (replaces runtime Line Trace)
        └→ PCG LOD distance control (Full 10m / Medium 30m / Low 100m / Culled)
                │
    TiledFileProvider (vector data) + CesiumBridge (elevation)
                │
    PolygonDeriver (elevation priority: DEM cache > DataProvider)
                │
    Cesium 3D Tiles terrain rendering + PCG procedural content
```

## Workflow

### Offline (Editor)

**Mode A: Local Files**

1. Place GeoJSON at `Content/GISData/Region_01/osm_data.geojson`
2. (Optional) Place DEM tiles at `Content/GISData/Region_01/DEM/`
3. Drag `AGISWorldBuilder` into the level
4. Configure in Details panel:
   - `DataSourceType` = LocalFile
   - `GeoJsonPath`, `DEMPath` (optional)
   - `OriginLongitude` / `OriginLatitude`
5. Click **Generate In Editor** to preview
6. Click **Generate And Save Data Asset** to persist as `.uasset`

**Mode B: ArcGIS REST**

1. Configure in Details panel:
   - `DataSourceType` = ArcGISRest
   - `FeatureServiceUrl`, `ArcGISApiKey`
   - `AdditionalLayerUrls` (optional, multi-layer)
   - `QueryBounds` (optional)
2. Click **Generate And Save Data Asset**

**Mode C: Existing DataAsset**

1. `DataSourceType` = DataAsset
2. Reference an existing `ULandUseMapDataAsset`
3. Click **Generate In Editor**

**Mode D: Tiled File (Large-scale)**

1. Run the Python preprocessing pipeline to generate tiled data:
   ```
   python Tools/GISPreprocess/preprocess.py --input ./RawData \
       --output Content/GISData/Region_01 --tile-size 1024 \
       --origin-lon 121.47 --origin-lat 31.23
   ```
2. Option A: Use `AGISWorldBuilder`
   - `DataSourceType` = TiledFile
   - `TileManifestPath` = `GISData/Region_01/tile_manifest.json`
   - Click **Generate And Save Data Asset**
3. Option B: Use `ATiledWorldBuilder` (batch mode, recommended for large areas)
   - Drag `ATiledWorldBuilder` into the level
   - Set `ManifestPath`, `ClassifyRules`
   - Click **Generate All Tiles**
   - Produces per-tile DataAssets + a `UTiledLandUseMapDataAsset` catalog

**Mode E: CesiumTiled (Global Scale + 3D Earth)**

1. (Optional) Install [Cesium for Unreal](https://cesium.com/platform/cesium-for-unreal/) — works without it via Mercator fallback
2. Generate offline DEM elevation cache:
   ```bash
   python Tools/GISPreprocess/srtm_to_terrain.py /path/to/N31E121.hgt \
       -o Content/GISData/Region_01 --tile-size 1000 --grid-size 100
   ```
3. Configure `AGISWorldBuilder`:
   - `DataSourceType` = CesiumTiled
   - `TileManifestPath` = `GISData/Region_01/tile_manifest.json`
   - `DEMCacheDirectory` = `GISData/Region_01/dem_cache/`
   - (If Cesium installed) `CesiumGeoreferenceActor` = scene's CesiumGeoreference
4. Click **Generate In Editor** → terrain-aware polygons with Cesium 3D earth rendering

No Cesium ion account required — all data is self-hosted.

### Online (Runtime / PCG)

1. Add **GIS Land Use Sampler** node in PCG Graph
   - `LandUseDataAsset` = reference to offline `.uasset`
   - `SamplingInterval` = 10m (configurable)
   - `FilterTypes` = [Forest] (optional type filter)
   - `bJitterPoints` = true
2. PCG node outputs points with metadata attributes:
   - `LandUseType`, `PolygonID`, `BuildingDensity`, `VegetationDensity`
3. Downstream PCG nodes use Attribute Filters to drive spawners per land use type

## Module Inventory

| Module | Status | Description |
|--------|--------|-------------|
| **IGISDataProvider** | 100% | Abstract data source interface |
| **LocalFileProvider** | 100% | Local GeoJSON + DEM file reader with caching |
| **ArcGISRestProvider** | 100% | HTTP queries, pagination, multi-layer, API key auth |
| **GeoJsonParser** | 100% | Full GeoJSON parsing, all geometry types, OSM category inference |
| **GISCoordinate** | 100% | Geo ↔ UE5 world + WGS84 ↔ UTM + Cesium ECEF mode |
| **DEMParser** | 85% | PNG/RAW/HGT complete; GeoTIFF simplified (needs GDAL for full support) |
| **TerrainAnalyzer** | 100% | 5-step pipeline: elevation grid -> slope/aspect -> classify -> CCL -> boundary extraction |
| **PolygonDeriver** | 100% | Two-mode polygon generation with iterative Sutherland-Hodgman polygon-line cutting |
| **RoadNetworkGraph** | 100% | Planar graph with O(n^2) intersection detection and edge splitting |
| **LandUseClassifier** | 100% | 9-rule classification + PCG param assignment + ESA WorldCover raster fusion |
| **PCGGISNode** | 100% | Grid sampling with point-in-polygon test, tile-aware execution, 4 metadata attributes |
| **GISWorldBuilder** | 100% | 5-mode orchestrator (LocalFile / ArcGIS / DataAsset / TiledFile / CesiumTiled), editor preview, async |
| **LandUseMapDataAsset** | 100% | Persistent polygon storage with grid spatial index + async tile loading |
| **GISPolygonComponent** | 100% | Actor component with ray-cast point-in-polygon, debug visualization |
| **CesiumBridgeComponent** | 100% | LLH↔UE5 coordinate bridge, offline DEM cache, PCG LOD distance control |
| **TiledFileProvider** | 100% | Tile manifest reader, LRU-cached per-tile GeoJSON/LandCover loading |
| **TileManifest** | 100% | JSON manifest parser, tile coordinate index, bounds query |
| **RasterLandCoverParser** | 100% | ESA WorldCover JSON raster parser with majority-class voting |
| **TiledLandUseMapDataAsset** | 100% | Per-tile DataAsset catalog with async streaming (FStreamableManager) |
| **TiledWorldBuilder** | 100% | Editor batch tool: manifest → per-tile PolygonDeriver → DataAsset catalog |
| **LandCoverGrid** | 100% | ESA WorldCover shared types (EWorldCoverClass, FLandCoverGrid) |

## Key Algorithms

### CutPolygonWithLines (PolygonDeriver)

Iterative polygon splitting using a Sutherland-Hodgman variant:
- For each cut polyline (road/river/coastline), for each segment:
  - Find intersection pairs with polygon boundary
  - Split polygon into left/right halves along the extended line
  - Replace polygon in working set with the two halves
- Filters degenerate fragments (area < threshold)

### ComputeIntersectionsAndSplit (RoadNetworkGraph)

O(n^2) edge-pair scan for planar graph construction:
- Detect all segment-segment intersections (excluding shared endpoints)
- Insert new nodes at crossing points (with dedup tolerance)
- Split original edges into sub-edge chains
- Prerequisite for Planar Face Extraction via `GetLeftmostTurn()`

### TerrainAnalyzer Pipeline

1. **BuildElevationGrid** - regular grid from DEM tiles
2. **ComputeSlopeAspect** - Horn's 3x3 Sobel method
3. **ClassifyTerrainGrid** - elevation/slope -> 6 terrain classes
4. **LabelConnectedZones** - Two-pass CCL with Union-Find + small zone merge
5. **ExtractZoneBoundaries** - contour tracing -> polygon vertices

### Offline DEM Elevation Cache (CesiumBridgeComponent)

Binary cache format per tile: `[int32 GridSize][float CellSizeM][4×double GeoBounds][float×GridSize²]`
- O(1) bilinear interpolation lookup (~5ns/query)
- Elevation priority: DEM cache → DataProvider::QueryElevation → fallback 0
- Thread-local last-hit cache for amortized O(1) tile lookup in inner loops

### PCG LOD Distance Control (CesiumBridgeComponent)

3-tier sampling density linked to camera distance:
- Full detail (≤1km): 10m sampling interval
- Medium detail (≤3km): 30m sampling interval
- Low detail (≤7km): 100m sampling interval
- Culled (>7km): no sampling

## File Structure

```
Plugins/GISProcedural/
├── GISProcedural.uplugin
├── Source/GISProcedural/
│   ├── GISProcedural.Build.cs
│   ├── Public/
│   │   ├── GISProceduralModule.h
│   │   ├── Data/
│   │   │   ├── IGISDataProvider.h
│   │   │   ├── GeoRect.h
│   │   │   ├── GISFeature.h
│   │   │   ├── GISCoordinate.h
│   │   │   ├── GeoJsonParser.h
│   │   │   ├── LocalFileProvider.h
│   │   │   ├── ArcGISRestProvider.h
│   │   │   ├── LandUseMapDataAsset.h
│   │   │   ├── TileManifest.h
│   │   │   ├── TiledFileProvider.h
│   │   │   ├── TiledLandUseMapDataAsset.h
│   │   │   ├── RasterLandCoverParser.h
│   │   │   └── LandCoverGrid.h
│   │   ├── DEM/
│   │   │   ├── DEMTypes.h
│   │   │   ├── DEMParser.h
│   │   │   └── TerrainAnalyzer.h
│   │   ├── Polygon/
│   │   │   ├── LandUsePolygon.h
│   │   │   ├── PolygonDeriver.h
│   │   │   ├── RoadNetworkGraph.h
│   │   │   └── LandUseClassifier.h
│   │   ├── PCG/
│   │   │   ├── PCGGISNode.h
│   │   │   └── PCGLandUseData.h
│   │   ├── Components/
│   │   │   └── GISPolygonComponent.h
│   │   └── Runtime/
│   │       ├── GISWorldBuilder.h
│   │       ├── TiledWorldBuilder.h
│   │       └── CesiumBridgeComponent.h
│   └── Private/
│       ├── GISProceduralModule.cpp
│       ├── Data/
│       │   ├── GISCoordinate.cpp
│       │   ├── GeoJsonParser.cpp
│       │   ├── LocalFileProvider.cpp
│       │   ├── ArcGISRestProvider.cpp
│       │   ├── LandUseMapDataAsset.cpp
│       │   ├── TileManifest.cpp
│       │   ├── TiledFileProvider.cpp
│       │   ├── TiledLandUseMapDataAsset.cpp
│       │   └── RasterLandCoverParser.cpp
│       ├── DEM/
│       │   ├── DEMParser.cpp
│       │   └── TerrainAnalyzer.cpp
│       ├── Polygon/
│       │   ├── PolygonDeriver.cpp
│       │   ├── RoadNetworkGraph.cpp
│       │   └── LandUseClassifier.cpp
│       ├── PCG/
│       │   ├── PCGGISNode.cpp
│       │   └── PCGLandUseData.cpp
│       ├── Components/
│       │   └── GISPolygonComponent.cpp
│       └── Runtime/
│           ├── GISWorldBuilder.cpp
│           ├── TiledWorldBuilder.cpp
│           └── CesiumBridgeComponent.cpp
```

## Python Preprocessing Pipeline

Located at `Tools/GISPreprocess/`. Converts raw GIS data into tiled format.

```
python preprocess.py --input ./RawData --output Content/GISData/Region_01 \
    --tile-size 1024 --origin-lon 121.47 --origin-lat 31.23
```

| Script | Purpose |
|--------|---------|
| `preprocess.py` | Main entry point, orchestrates all steps |
| `pbf_to_geojson.py` | PBF → GeoJSON via osmium/ogr2ogr |
| `tile_cutter.py` | Vector tile cutting with cross-boundary feature tracking |
| `projection.py` | WGS84 ↔ UTM conversion (matches UE5 GISCoordinate) |
| `raster_classifier.py` | ESA WorldCover GeoTIFF → per-tile landcover.json |
| `dem_cropper.py` | DEM GeoTIFF → per-tile 16bit PNG |
| `manifest_writer.py` | Generates tile_manifest.json |
| `validate.py` | Output integrity validation |
| `srtm_to_terrain.py` | SRTM/GeoTIFF → binary elevation cache for CesiumTiled mode |

**Dependencies**: Python 3.10+. Optional: GDAL (DEM/raster), osmium-tool (PBF), numpy (DEM cache generation).

## Known Limitations

- **DEMParser GeoTIFF**: Simplified TIFF reader; full GeoTIFF support requires GDAL/libgeotiff integration. Workaround: use `ManualTileInfo` property or PNG/RAW formats.
- **LandUseClassifier water detection**: Optional enhancement for water vector data; classification works without it using DEM-based water candidate detection.
- **Multi-tile DEM merge**: TiledFileProvider currently returns single-tile DEM only; cross-tile DEM queries require future DEMParser enhancement.
- **Multi-tile LandCover merge**: Cross-tile LandCover queries not yet supported; single-tile queries work correctly.
- **Cesium soft dependency**: WITH_CESIUM=0 when CesiumForUnreal plugin is not installed. CesiumBridgeComponent falls back to Mercator coordinate mode; all coordinate and elevation APIs still work.
