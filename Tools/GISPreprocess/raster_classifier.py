"""
ESA WorldCover 栅格分类处理器
将 WorldCover GeoTIFF 按瓦片裁切，输出 per-tile landcover.json。

输出格式与 UE5 FLandCoverGrid / RasterLandCoverParser 对齐：
{
    "width": 100,
    "height": 100,
    "cell_size_m": 10.0,
    "classes": [10, 10, 50, 30, ...]  // ESA WorldCover 类码，行优先
}
"""
import json
import math
from pathlib import Path
from typing import List

try:
    from osgeo import gdal
    HAS_GDAL = True
except ImportError:
    HAS_GDAL = False

# ESA WorldCover v200 类码
WORLDCOVER_CLASSES = {
    10: "Tree cover",
    20: "Shrubland",
    30: "Grassland",
    40: "Cropland",
    50: "Built-up",
    60: "Bare / sparse vegetation",
    70: "Snow and ice",
    80: "Permanent water bodies",
    90: "Herbaceous wetland",
    95: "Mangroves",
    100: "Moss and lichen",
}

# 默认 WorldCover 分辨率（10m）
DEFAULT_RESOLUTION_M = 10.0


class RasterClassifier:
    """处理 ESA WorldCover 栅格数据，按 tile 裁切输出 JSON"""

    def __init__(self, resolution_m: float = DEFAULT_RESOLUTION_M):
        self.resolution_m = resolution_m

    def process_tiles(self, landcover_files: List[Path], tile_entries,
                      tiles_dir: Path):
        """
        裁切 WorldCover 到每个 tile，输出 landcover.json

        Args:
            landcover_files: WorldCover GeoTIFF 文件列表
            tile_entries: TileEntry 列表
            tiles_dir: 瓦片输出根目录
        """
        if not landcover_files:
            print("  No LandCover files to process")
            return

        if not HAS_GDAL:
            print("  [WARN] GDAL not available, generating synthetic LandCover")
            self._write_synthetic_landcover(tile_entries, tiles_dir)
            return

        # 构建 VRT
        vrt_path = str(tiles_dir / "_temp_landcover.vrt")
        gdal.BuildVRT(vrt_path, [str(f) for f in landcover_files])
        src_ds = gdal.Open(vrt_path)

        if not src_ds:
            print("  [WARN] Failed to open LandCover VRT, generating synthetic")
            self._write_synthetic_landcover(tile_entries, tiles_dir)
            return

        band = src_ds.GetRasterBand(1)
        gt = src_ds.GetGeoTransform()  # (origin_x, pixel_w, 0, origin_y, 0, pixel_h)

        processed = 0
        for entry in tile_entries:
            min_lon, min_lat, max_lon, max_lat = entry.geo_bounds
            tile_dir = tiles_dir / f"tile_{entry.col}_{entry.row}"
            tile_dir.mkdir(parents=True, exist_ok=True)

            # 计算像素范围
            col_start = int((min_lon - gt[0]) / gt[1])
            col_end = int((max_lon - gt[0]) / gt[1])
            row_start = int((max_lat - gt[3]) / gt[5])  # gt[5] 是负数
            row_end = int((min_lat - gt[3]) / gt[5])

            # 裁剪到栅格范围
            col_start = max(0, min(col_start, src_ds.RasterXSize - 1))
            col_end = max(0, min(col_end, src_ds.RasterXSize - 1))
            row_start = max(0, min(row_start, src_ds.RasterYSize - 1))
            row_end = max(0, min(row_end, src_ds.RasterYSize - 1))

            width = col_end - col_start
            height = row_end - row_start

            if width <= 0 or height <= 0:
                continue

            try:
                data = band.ReadAsArray(col_start, row_start, width, height)
                if data is None:
                    continue

                classes = data.flatten().tolist()

                # 计算实际分辨率
                cell_size_m = abs(gt[1]) * 111320 * math.cos(
                    math.radians((min_lat + max_lat) / 2))

                lc_data = {
                    "width": width,
                    "height": height,
                    "cell_size_m": round(cell_size_m, 2),
                    "classes": classes,
                }

                out_path = tile_dir / "landcover.json"
                with open(out_path, "w") as f:
                    json.dump(lc_data, f)

                entry.landcover_rel_path = f"tiles/tile_{entry.col}_{entry.row}/landcover.json"
                processed += 1
            except Exception as e:
                print(f"    [WARN] LandCover failed for tile_{entry.col}_{entry.row}: {e}")

        src_ds = None
        vrt_file = Path(vrt_path)
        if vrt_file.exists():
            vrt_file.unlink()

        print(f"  Processed LandCover for {processed}/{len(tile_entries)} tiles")

    def _write_synthetic_landcover(self, tile_entries, tiles_dir: Path):
        """无 GDAL 时生成合成 LandCover 数据（占位）"""
        for entry in tile_entries:
            tile_dir = tiles_dir / f"tile_{entry.col}_{entry.row}"
            tile_dir.mkdir(parents=True, exist_ok=True)

            # 默认 10x10 网格，全部标记为 Grassland(30)
            grid_size = 10
            lc_data = {
                "width": grid_size,
                "height": grid_size,
                "cell_size_m": self.resolution_m,
                "classes": [30] * (grid_size * grid_size),
            }

            out_path = tile_dir / "landcover.json"
            with open(out_path, "w") as f:
                json.dump(lc_data, f)

            entry.landcover_rel_path = f"tiles/tile_{entry.col}_{entry.row}/landcover.json"

        print(f"  Generated {len(tile_entries)} synthetic LandCover files")
