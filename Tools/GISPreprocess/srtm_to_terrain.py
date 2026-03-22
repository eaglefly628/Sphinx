#!/usr/bin/env python3
"""
SRTM DEM → Cesium Quantized Mesh + 离线高程缓存

两种输出：
1. Quantized Mesh tiles（供 Cesium 3D Tiles 渲染地形）
2. 离线高程网格缓存 elevation_X_Y.bin（供 UE 侧 CesiumBridge O(1) 查表）

高程缓存文件格式（小端序）：
  [int32  GridSize]      — 网格边长（如 100 → 100×100）
  [float  CellSizeM]     — 网格间距（米）
  [double MinLon]
  [double MinLat]
  [double MaxLon]
  [double MaxLat]
  [float × GridSize²]    — 高程值（米），行优先

依赖：
  pip install numpy rasterio
  可选（Quantized Mesh）：pip install quantized-mesh-encoder
"""

import argparse
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

try:
    import rasterio
    from rasterio.windows import from_bounds
except ImportError:
    rasterio = None


def read_srtm_hgt(hgt_path: Path) -> tuple[np.ndarray, dict]:
    """读取 SRTM .hgt 文件（1-arc-second 或 3-arc-second）"""
    file_size = hgt_path.stat().st_size
    if file_size == 3601 * 3601 * 2:
        rows = cols = 3601  # 1-arc-second
    elif file_size == 1201 * 1201 * 2:
        rows = cols = 1201  # 3-arc-second
    else:
        raise ValueError(f"Unexpected .hgt file size: {file_size}")

    data = np.fromfile(str(hgt_path), dtype=">i2").reshape((rows, cols))
    # 将 void 值 (-32768) 替换为 0
    data = np.where(data == -32768, 0, data).astype(np.float32)

    # 从文件名推断经纬度范围 (例如 N31E121.hgt)
    name = hgt_path.stem.upper()
    lat_sign = 1 if name[0] == "N" else -1
    lon_sign = 1 if name[3] == "E" else -1
    lat = lat_sign * int(name[1:3])
    lon = lon_sign * int(name[4:7])

    meta = {
        "min_lon": float(lon),
        "min_lat": float(lat),
        "max_lon": float(lon + 1),
        "max_lat": float(lat + 1),
        "rows": rows,
        "cols": cols,
    }
    return data, meta


def read_dem_rasterio(dem_path: Path) -> tuple[np.ndarray, dict]:
    """用 rasterio 读取 GeoTIFF/其他格式 DEM"""
    if rasterio is None:
        raise ImportError("rasterio is required for GeoTIFF support: pip install rasterio")

    with rasterio.open(str(dem_path)) as src:
        data = src.read(1).astype(np.float32)
        bounds = src.bounds
        meta = {
            "min_lon": bounds.left,
            "min_lat": bounds.bottom,
            "max_lon": bounds.right,
            "max_lat": bounds.top,
            "rows": data.shape[0],
            "cols": data.shape[1],
        }
    return data, meta


def read_dem(dem_path: Path) -> tuple[np.ndarray, dict]:
    """自动检测格式并读取 DEM"""
    suffix = dem_path.suffix.lower()
    if suffix == ".hgt":
        return read_srtm_hgt(dem_path)
    elif suffix in (".tif", ".tiff"):
        return read_dem_rasterio(dem_path)
    else:
        # 尝试 rasterio
        if rasterio is not None:
            return read_dem_rasterio(dem_path)
        raise ValueError(f"Unsupported DEM format: {suffix}")


def generate_elevation_cache(
    dem_data: np.ndarray,
    dem_meta: dict,
    tile_size_m: float,
    grid_size: int,
    output_dir: Path,
    tile_manifest: dict | None = None,
) -> list[dict]:
    """
    将 DEM 切分为 tile 并生成离线高程缓存

    Args:
        dem_data: 高程数据数组
        dem_meta: 元数据（经纬度范围等）
        tile_size_m: tile 边长（米）
        grid_size: 每 tile 的高程网格边长
        output_dir: 输出目录
        tile_manifest: 可选，已有的 tile_manifest 用于对齐 tile 坐标

    Returns:
        生成的 tile 缓存列表
    """
    output_dir.mkdir(parents=True, exist_ok=True)

    min_lon = dem_meta["min_lon"]
    min_lat = dem_meta["min_lat"]
    max_lon = dem_meta["max_lon"]
    max_lat = dem_meta["max_lat"]

    # 计算每度的米数
    center_lat = (min_lat + max_lat) / 2.0
    meters_per_deg_lon = (math.pi / 180.0) * 6371000.0 * math.cos(math.radians(center_lat))
    meters_per_deg_lat = (math.pi / 180.0) * 6371000.0

    # tile 边长对应的度数
    tile_deg_lon = tile_size_m / meters_per_deg_lon
    tile_deg_lat = tile_size_m / meters_per_deg_lat

    # 如果有 manifest，使用其 origin 对齐
    origin_lon = min_lon
    origin_lat = min_lat
    if tile_manifest:
        origin_lon = tile_manifest.get("origin_lon", min_lon)
        origin_lat = tile_manifest.get("origin_lat", min_lat)

    cell_size_m = tile_size_m / grid_size

    generated = []
    tile_x = 0
    lon = origin_lon
    while lon < max_lon:
        tile_y = 0
        lat = origin_lat
        while lat < max_lat:
            t_min_lon = lon
            t_min_lat = lat
            t_max_lon = min(lon + tile_deg_lon, max_lon)
            t_max_lat = min(lat + tile_deg_lat, max_lat)

            # 从 DEM 采样网格
            elev_grid = sample_dem_grid(
                dem_data, dem_meta,
                t_min_lon, t_min_lat, t_max_lon, t_max_lat,
                grid_size,
            )

            # 写入二进制缓存
            filename = f"elevation_{tile_x}_{tile_y}.bin"
            filepath = output_dir / filename
            write_elevation_cache(
                filepath, elev_grid, grid_size, cell_size_m,
                t_min_lon, t_min_lat, t_max_lon, t_max_lat,
            )

            generated.append({
                "file": filename,
                "tile_x": tile_x,
                "tile_y": tile_y,
                "bounds": {
                    "min_lon": t_min_lon, "min_lat": t_min_lat,
                    "max_lon": t_max_lon, "max_lat": t_max_lat,
                },
            })

            lat += tile_deg_lat
            tile_y += 1

        lon += tile_deg_lon
        tile_x += 1

    return generated


def sample_dem_grid(
    dem_data: np.ndarray,
    dem_meta: dict,
    min_lon: float, min_lat: float,
    max_lon: float, max_lat: float,
    grid_size: int,
) -> np.ndarray:
    """从 DEM 数据中采样 grid_size × grid_size 的高程网格"""
    rows, cols = dem_data.shape
    result = np.zeros((grid_size, grid_size), dtype=np.float32)

    for row in range(grid_size):
        lat = min_lat + (max_lat - min_lat) * row / max(grid_size - 1, 1)
        # DEM 行号（从上到下，纬度从高到低）
        dem_row = (dem_meta["max_lat"] - lat) / (dem_meta["max_lat"] - dem_meta["min_lat"]) * (rows - 1)
        dem_row = max(0, min(rows - 1, dem_row))

        for col in range(grid_size):
            lon = min_lon + (max_lon - min_lon) * col / max(grid_size - 1, 1)
            dem_col = (lon - dem_meta["min_lon"]) / (dem_meta["max_lon"] - dem_meta["min_lon"]) * (cols - 1)
            dem_col = max(0, min(cols - 1, dem_col))

            # 双线性插值
            r0 = int(dem_row)
            c0 = int(dem_col)
            r1 = min(r0 + 1, rows - 1)
            c1 = min(c0 + 1, cols - 1)
            fr = dem_row - r0
            fc = dem_col - c0

            v00 = dem_data[r0, c0]
            v10 = dem_data[r0, c1]
            v01 = dem_data[r1, c0]
            v11 = dem_data[r1, c1]

            result[row, col] = (
                v00 * (1 - fr) * (1 - fc) +
                v10 * (1 - fr) * fc +
                v01 * fr * (1 - fc) +
                v11 * fr * fc
            )

    return result


def write_elevation_cache(
    filepath: Path,
    grid: np.ndarray,
    grid_size: int,
    cell_size_m: float,
    min_lon: float, min_lat: float,
    max_lon: float, max_lat: float,
):
    """写入 UE 侧 CesiumBridge 可读的二进制高程缓存"""
    with open(filepath, "wb") as f:
        f.write(struct.pack("<i", grid_size))
        f.write(struct.pack("<f", cell_size_m))
        f.write(struct.pack("<d", min_lon))
        f.write(struct.pack("<d", min_lat))
        f.write(struct.pack("<d", max_lon))
        f.write(struct.pack("<d", max_lat))
        f.write(grid.astype("<f4").tobytes())


def main():
    parser = argparse.ArgumentParser(
        description="SRTM/GeoTIFF DEM → Cesium terrain + UE elevation cache"
    )
    parser.add_argument("dem_path", help="DEM file path (.hgt or .tif)")
    parser.add_argument("-o", "--output", default="output", help="Output directory")
    parser.add_argument("--tile-size", type=float, default=1000.0, help="Tile size in meters (default: 1000)")
    parser.add_argument("--grid-size", type=int, default=100, help="Elevation grid size per tile (default: 100)")
    parser.add_argument("--manifest", help="Existing tile_manifest.json for tile alignment")
    args = parser.parse_args()

    dem_path = Path(args.dem_path)
    if not dem_path.exists():
        print(f"Error: DEM file not found: {dem_path}", file=sys.stderr)
        sys.exit(1)

    print(f"Reading DEM: {dem_path}")
    dem_data, dem_meta = read_dem(dem_path)
    print(f"  Shape: {dem_data.shape}, Range: [{dem_data.min():.1f}, {dem_data.max():.1f}]m")
    print(f"  Bounds: ({dem_meta['min_lon']:.4f}, {dem_meta['min_lat']:.4f}) → "
          f"({dem_meta['max_lon']:.4f}, {dem_meta['max_lat']:.4f})")

    manifest = None
    if args.manifest:
        with open(args.manifest) as f:
            manifest = json.load(f)

    output_dir = Path(args.output) / "dem_cache"
    print(f"\nGenerating elevation cache → {output_dir}")
    print(f"  Tile size: {args.tile_size}m, Grid: {args.grid_size}×{args.grid_size}")

    tiles = generate_elevation_cache(
        dem_data, dem_meta,
        tile_size_m=args.tile_size,
        grid_size=args.grid_size,
        output_dir=output_dir,
        tile_manifest=manifest,
    )

    print(f"  Generated {len(tiles)} tile cache files")

    # 写入索引文件
    index_path = output_dir / "dem_cache_index.json"
    index = {
        "source_dem": str(dem_path),
        "tile_size_m": args.tile_size,
        "grid_size": args.grid_size,
        "cell_size_m": args.tile_size / args.grid_size,
        "tiles": tiles,
    }
    with open(index_path, "w") as f:
        json.dump(index, f, indent=2)
    print(f"  Index: {index_path}")

    # 统计
    total_bytes = sum((output_dir / t["file"]).stat().st_size for t in tiles)
    print(f"\n  Total cache size: {total_bytes / 1024:.1f} KB ({total_bytes / 1024 / 1024:.2f} MB)")
    print(f"  Per tile: ~{total_bytes / max(len(tiles), 1) / 1024:.1f} KB")


if __name__ == "__main__":
    main()
