"""
Sphinx GIS Preprocessing Pipeline
==================================
将原始 GIS 数据（PBF/GeoJSON, DEM GeoTIFF, ESA WorldCover）
切分为 tile_manifest.json + 瓦片目录结构，供 UE5 TiledFileProvider 读取。

用法:
    python preprocess.py --input ./RawData --output Content/GISData/Region_01 \\
        --tile-size 1024 --origin-lon 121.47 --origin-lat 31.23

依赖: pip install -r requirements.txt
"""
import argparse
import sys
from pathlib import Path

from projection import wgs84_to_utm, utm_zone_from_lon
from tile_cutter import TileCutter
from pbf_to_geojson import pbf_to_geojson
from dem_cropper import DEMCropper
from raster_classifier import RasterClassifier
from manifest_writer import write_manifest
from validate import validate_output


def parse_args():
    parser = argparse.ArgumentParser(
        description="Sphinx GIS Preprocessing Pipeline")
    parser.add_argument("--input", "-i", required=True,
                        help="Input directory containing raw GIS data")
    parser.add_argument("--output", "-o", required=True,
                        help="Output directory for tiled data")
    parser.add_argument("--tile-size", type=float, default=1024.0,
                        help="Tile size in meters (default: 1024)")
    parser.add_argument("--origin-lon", type=float, required=True,
                        help="Origin longitude (WGS84)")
    parser.add_argument("--origin-lat", type=float, required=True,
                        help="Origin latitude (WGS84)")
    parser.add_argument("--skip-validation", action="store_true",
                        help="Skip output validation step")
    parser.add_argument("--skip-landcover", action="store_true",
                        help="Skip ESA WorldCover processing")
    parser.add_argument("--skip-dem", action="store_true",
                        help="Skip DEM processing")
    return parser.parse_args()


def find_input_files(input_dir: Path):
    """Discover input files by extension."""
    files = {
        "pbf": list(input_dir.glob("*.osm.pbf")),
        "geojson": list(input_dir.glob("*.geojson")) + list(input_dir.glob("*.json")),
        "dem": list(input_dir.glob("**/*.tif")) + list(input_dir.glob("**/*.hgt")),
        "landcover": list(input_dir.glob("**/ESA_WorldCover*.tif"))
                     + list(input_dir.glob("**/worldcover*.tif")),
    }
    return files


def main():
    args = parse_args()
    input_dir = Path(args.input)
    output_dir = Path(args.output)

    if not input_dir.exists():
        print(f"[ERROR] Input directory not found: {input_dir}")
        sys.exit(1)

    output_dir.mkdir(parents=True, exist_ok=True)
    tiles_dir = output_dir / "tiles"
    tiles_dir.mkdir(exist_ok=True)

    # ---- 1. 发现输入文件 ----
    print("=" * 60)
    print("[1/6] Discovering input files...")
    files = find_input_files(input_dir)
    print(f"  PBF files:       {len(files['pbf'])}")
    print(f"  GeoJSON files:   {len(files['geojson'])}")
    print(f"  DEM files:       {len(files['dem'])}")
    print(f"  LandCover files: {len(files['landcover'])}")

    # ---- 2. PBF → GeoJSON（如果需要） ----
    geojson_files = files["geojson"]
    if files["pbf"] and not geojson_files:
        print("=" * 60)
        print("[2/6] Converting PBF to GeoJSON...")
        for pbf_file in files["pbf"]:
            out_geojson = output_dir / (pbf_file.stem.replace(".osm", "") + ".geojson")
            pbf_to_geojson(pbf_file, out_geojson)
            geojson_files.append(out_geojson)
    else:
        print("[2/6] Skipping PBF conversion (GeoJSON already available)")

    if not geojson_files:
        print("[ERROR] No GeoJSON or PBF files found in input directory")
        sys.exit(1)

    # ---- 3. 计算 UTM 区域和瓦片网格 ----
    print("=" * 60)
    print("[3/6] Computing tile grid...")
    utm_zone = utm_zone_from_lon(args.origin_lon)
    northern = args.origin_lat >= 0
    print(f"  UTM Zone: {utm_zone}{'N' if northern else 'S'}")
    print(f"  Origin: ({args.origin_lon}, {args.origin_lat})")
    print(f"  Tile size: {args.tile_size}m")

    # ---- 4. 矢量瓦片切割 ----
    print("=" * 60)
    print("[4/6] Cutting vector data into tiles...")
    cutter = TileCutter(
        tile_size_m=args.tile_size,
        origin_lon=args.origin_lon,
        origin_lat=args.origin_lat,
        utm_zone=utm_zone,
        northern_hemisphere=northern,
    )
    tile_entries = cutter.cut_geojson(geojson_files, tiles_dir)
    print(f"  Generated {len(tile_entries)} tiles")

    # ---- 5. DEM 裁切 ----
    if not args.skip_dem and files["dem"]:
        print("=" * 60)
        print("[5/6] Cropping DEM tiles...")
        dem_cropper = DEMCropper()
        dem_landcover_files = [f for f in files["dem"] if "worldcover" not in f.name.lower()
                               and "ESA_WorldCover" not in f.name]
        dem_cropper.crop_to_tiles(dem_landcover_files, tile_entries, tiles_dir)
    else:
        print("[5/6] Skipping DEM processing")

    # ---- 6. LandCover 栅格 ----
    if not args.skip_landcover and files["landcover"]:
        print("=" * 60)
        print("[6/6] Processing ESA WorldCover raster...")
        classifier = RasterClassifier()
        classifier.process_tiles(files["landcover"], tile_entries, tiles_dir)
    else:
        print("[6/6] Skipping LandCover processing")

    # ---- 写清单 ----
    print("=" * 60)
    print("Writing tile_manifest.json...")
    write_manifest(
        output_path=output_dir / "tile_manifest.json",
        tile_entries=tile_entries,
        tile_size_m=args.tile_size,
        utm_zone=utm_zone,
        northern=northern,
        origin_lon=args.origin_lon,
        origin_lat=args.origin_lat,
    )

    # ---- 验证 ----
    if not args.skip_validation:
        print("=" * 60)
        print("Validating output...")
        ok = validate_output(output_dir)
        if not ok:
            print("[WARN] Validation found issues, check output above")
        else:
            print("[OK] Validation passed")

    print("=" * 60)
    print(f"Done! Output: {output_dir}")
    print(f"  Tiles: {len(tile_entries)}")
    print(f"  Manifest: {output_dir / 'tile_manifest.json'}")


if __name__ == "__main__":
    main()
