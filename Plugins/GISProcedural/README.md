# GISProcedural Plugin

Unreal Engine 5 plugin for procedural world generation from real-world GIS data (GeoJSON, DEM, ArcGIS REST).

## Architecture

```
Data Sources              Offline Processing           Persistence          Runtime
─────────────            ──────────────────           ───────────          ───────

GeoJSON files ──┐
                ├→ IGISDataProvider → PolygonDeriver → DataAsset  → PCG Node → Sample Points
ArcGIS REST  ──┘         │                  │         (.uasset)       │
                         │                  │                         │
(Optional) DEM ────→ QueryElevation    TerrainAnalyzer          Attribute Filter
                         │                  │                         │
                         │            ┌─ With DEM: vector cutting    ├─ Tree HISM
                         │            └─ No DEM: vector polygons     ├─ Building HISM
                         │                  │                        └─ Water Mesh
                         │            LandUseClassifier
                         │                  │
                         │            AssignPCGParams
                         │                  │
                         └────────→ TArray<FLandUsePolygon>
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
| **GISCoordinate** | 100% | Bidirectional geo <-> world coordinate conversion |
| **DEMParser** | 85% | PNG/RAW/HGT complete; GeoTIFF simplified (needs GDAL for full support) |
| **TerrainAnalyzer** | 100% | 5-step pipeline: elevation grid -> slope/aspect -> classify -> CCL -> boundary extraction |
| **PolygonDeriver** | 100% | Two-mode polygon generation with iterative Sutherland-Hodgman polygon-line cutting |
| **RoadNetworkGraph** | 100% | Planar graph with O(n^2) intersection detection and edge splitting |
| **LandUseClassifier** | 100% | 9-rule classification + PCG param assignment + ESA WorldCover raster fusion |
| **PCGGISNode** | 100% | Grid sampling with point-in-polygon test, tile-aware execution, 4 metadata attributes |
| **GISWorldBuilder** | 100% | 4-mode orchestrator (LocalFile / ArcGIS / DataAsset / TiledFile), editor preview, async |
| **LandUseMapDataAsset** | 100% | Persistent polygon storage with grid spatial index + async tile loading |
| **GISPolygonComponent** | 100% | Actor component with ray-cast point-in-polygon, debug visualization |
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
│   │       └── TiledWorldBuilder.h
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
│           └── TiledWorldBuilder.cpp
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

**Dependencies**: Python 3.10+. Optional: GDAL (DEM/raster), osmium-tool (PBF).

## Known Limitations

- **DEMParser GeoTIFF**: Simplified TIFF reader; full GeoTIFF support requires GDAL/libgeotiff integration. Workaround: use `ManualTileInfo` property or PNG/RAW formats.
- **LandUseClassifier water detection**: Optional enhancement for water vector data; classification works without it using DEM-based water candidate detection.
- **Multi-tile DEM merge**: TiledFileProvider currently returns single-tile DEM only; cross-tile DEM queries require future DEMParser enhancement.
- **Multi-tile LandCover merge**: Cross-tile LandCover queries not yet supported; single-tile queries work correctly.
