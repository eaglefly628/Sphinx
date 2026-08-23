# Reproducing the result inside Unreal (no C++, no Cesium)

Works with a blank UE 5.3+ project on any platform (tested target: 5.6 / Mac).

## Quick path (2 minutes, manual)

1. Create a blank project (Games ▸ Blank, no starter content needed).
2. Drag `output/ground.glb` into the Content Browser (Interchange imports it;
   accept defaults). Drag in `output/albedo.png` and `output/normal.png` too.
3. Right-click ▸ Material on `albedo` texture, open it, plug albedo into
   **Base Color** and normal (set its compression to Normalmap, sRGB off) into
   **Normal**. Apply the material to the imported mesh.
4. Drop the mesh into the level, set actor scale to **100** (mesh is in
   metres, UE is cm).

## Scripted path (full: textures + material + splat roughness + markers)

1. Edit ▸ Plugins ▸ enable **Python Editor Script Plugin**, restart editor.
2. Copy this repo folder (or at least `Tools/SatelliteGround/output` + `ue/`)
   somewhere on the machine running UE.
3. Window ▸ Output Log, switch the console dropdown from *Cmd* to *Python*:

   ```
   py "/ABS/PATH/Tools/SatelliteGround/ue/import_surface_tile.py"
   ```

   (If `output/` is not next to `ue/`, set `SATGROUND_OUTPUT=/abs/path/output`
   in the environment before launching UE, or edit `OUTPUT_DIR` in the script.)

4. Result: `/Game/SatGround` gets `SM_SatGround`, four textures and
   `M_SatGround` (albedo + normal, roughness = splat-weighted blend of the
   8 class layers); the current level gets the ground actor (scaled ×100)
   plus sphere markers for every detected ground object, in the correct
   east/north position. Save the level.

## Frame / georeference notes

- glTF is Y-up; UE's importer converts to Z-up, landing at **+X = east,
  +Y = south, +Z = up**, mesh centred on the tile centre.
- The tile's WGS84 anchor is in `surface_tile.json` (`geo`, `mesh.center_*`).
  Inside the EagleWalk project proper, place it with `GISCoordinate` /
  `CesiumGlobeAnchor` at `(center_lon, center_lat)` instead of world origin.
- The relief is real geometry, so expect the 3D parks/water/roads seen in the
  web viewer; the `10x` exaggeration slider there corresponds to scaling the
  actor's Z by the same factor if you want the dramatized look.

## Troubleshooting

- **GLB import dialog appears** → the script sets `automated=True`; if a
  dialog still shows (older Interchange), accept defaults.
- **Mesh is tiny** → actor scale must be 100 (metres → centimetres).
- **Material graph partially empty** → the splat-roughness section failed on
  an API mismatch; the material still works with albedo+normal. Check the
  Output Log for `[SatGround]` lines.
- **Markers float** → they spawn at a fixed 1.2 m height for visibility;
  they are position markers, not final props.
