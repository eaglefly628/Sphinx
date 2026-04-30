# EagleCloud

Bridges global satellite cloud cover imagery (NASA GIBS) into Ultra Dynamic Sky.

```
GIBS PNG (equirectangular global)
   │
   └─► UTexture2D
         │
         ├─► Macro shell sphere material  ── for far / space view
         │      (UV = sphere lon/lat)
         │
         └─► ASatelliteCloudFeeder        ── for near / ground view
              ├─ samples UV sub-rect for camera region
              ├─ DrawTexture into UDS Cloud Coverage RT
              └─ moves UDS Cloud Coverage Target Location with camera

       AAtmosphereCloudManager
         └─ blends macro shell ↔ UDS volumetric by camera altitude
            (drives MPC scalars + Feeder.AffectsGlobalValues)
```

## Phase A — Local test (single 200 km box)

1. Generate test PNG:
   ```
   python3 Tools/Weather/gen_cloud_test.py
   ```
2. Import `Tools/Weather/output/CloudSat_Test.png` into UE Content browser.
   - Compression: `Default (RGBA)` (NOT BC1)
   - sRGB: **off**
   - Tiling Method X: `Wrap`, Y: `Clamp`
3. Drop `ASatelliteCloudFeeder` actor in the UDS test level.
4. Assign `CloudTexture` only (leave `GlobalCloudTexture` empty).
5. Click `Apply To UDS` button or PIE — UDS volumetric clouds form per the
   pattern in 200 km × 200 km area at world origin.

## Phase B — Global sampling (recommended)

### 1. Fetch global cloud cover

```
python3 Tools/Weather/fetch_gibs_cloud.py                # latest True Color
python3 Tools/Weather/fetch_gibs_cloud.py --layer VIIRS --width 8192 --height 4096
```

Output: `Tools/Weather/output/CloudGlobal_<layer>_<date>.png` — full-globe
equirectangular (lon -180..180, lat -90..90).

### 2. Import into UE

- Drag `CloudGlobal_*.png` into Content browser.
- **Texture settings (critical)**:
  - Compression Settings: `Masks (no sRGB)` or `Default (RGBA)`
  - sRGB: **off** (linear data — UDS reads raw values)
  - Tiling Method X: `Wrap`, Y: `Clamp`
  - Mip Gen Settings: `From Texture Group` (default mips on, prevents moiré)

### 3. SatelliteCloudFeeder setup

- Drop `ASatelliteCloudFeeder` in level.
- Assign `GlobalCloudTexture`.
- Set `OriginLatitude` / `OriginLongitude` to match where UE world (0,0,0) is.
  - Example for Shanghai default origin: 31.23, 121.47.
  - For Cesium projects, match `CesiumGeoreference` origin.
- `CoverageRadiusKm = 100` (default → 200 km × 200 km local UDS window).
- `bFollowPlayerCamera = true` and `RefreshIntervalSeconds = 0.2` so the
  window slides with the camera as you fly.

UDS volumetric clouds will now reflect the global GIBS cloud field locally
within the 200 km window around the camera.

### 4. Macro atmosphere shell (visual layer for far view)

EagleCloud doesn't ship the sphere mesh asset (it's content-side). Create:

- **Mesh**: a sphere (engine `BasicShapes/Sphere` works), uniformly scaled to
  about `1.05 × planet radius` — slightly larger than the volumetric cloud
  layer so it covers cleanly.
- **Material** `M_AtmosphereShell`:
  - Blend Mode: `Translucent`
  - Shading Model: `Unlit` (or `Default Lit` if you want sun shading)
  - Disable Depth Test: optional (avoid Z-fighting with volumetric clouds in
    the transition band — see Gemini's tip)
  - Texture sample: GlobalCloudTexture, UV = sphere's built-in UV (for an
    engine sphere this maps lon/lat ≈ equirectangular)
  - Output Opacity: `BrightnessOfCloudSample × MacroAlpha` (where `MacroAlpha`
    is a Scalar Parameter Value driven by `MPC_AtmosphereCloud`)

### 5. Material Parameter Collection

Create `MPC_AtmosphereCloud` with three scalar parameters:

| Name        | Default | Used by                                |
|-------------|---------|----------------------------------------|
| MacroAlpha  | 0.0     | Atmosphere shell material opacity      |
| UDSDensity  | 1.0     | Optional: UDS cloud density mix shader |
| AltitudeKm  | 0.0     | Debug / atmospheric color tinting      |

### 6. AtmosphereCloudManager

- Drop `AAtmosphereCloudManager` in level.
- Assign:
  - `Feeder` → your `ASatelliteCloudFeeder`
  - `MacroShellActor` → the sphere mesh actor
  - `MPC` → `MPC_AtmosphereCloud`
- Tweak `LowAltitudeKm` (default 20 km) and `HighAltitudeKm` (default 500 km)
  for the LOD transition band.

## Coordinate convention

The flat-earth approximation in `SatelliteCloudFeeder::GetSampleCenterLatLon`
assumes:
- UE +X = east  → +longitude
- UE +Y = south → −latitude

Adjust the signs in `GetSampleCenterLatLon()` if your project uses different
axes (Cesium UE plugin sometimes flips Y).

For projects using `CesiumGeoreference`, swap `GetSampleCenterLatLon()` to
call `CesiumGeoreference->TransformUnrealPositionToLongitudeLatitudeHeight()`
— the rest of the pipeline is unchanged.

## Known caveats (per Gemini's review)

- **Z-fighting** in the transition band: set `Disable Depth Test` on the
  shell material, or tweak Translucency Sort Priority.
- **Lighting consistency**: the shell shows a 2D photo of clouds; the
  volumetric layer is fully lit. Bridge by either using the same sun light
  vector in shell shading, or applying a fake normal from texture brightness
  as heightmap.
- **UDS Tiling**: keep GlobalCloudTexture's X address mode = Wrap, Y = Clamp
  to avoid pole singularity and longitude seam tears.
- **Mips**: enable mip generation to prevent moiré when shell occupies a
  small fraction of the screen at orbital altitudes.
