"""
tile_manifest.json 生成器
输出格式与 UE5 FTileManifest 对齐。
"""
import json
from pathlib import Path
from typing import List


def write_manifest(output_path: Path, tile_entries: List,
                   tile_size_m: float, utm_zone: int, northern: bool,
                   origin_lon: float, origin_lat: float):
    """
    写入 tile_manifest.json

    Args:
        output_path: 输出文件路径
        tile_entries: TileEntry 列表
        tile_size_m: 瓦片大小（米）
        utm_zone: UTM 区号
        northern: 是否北半球
        origin_lon: 原点经度
        origin_lat: 原点纬度
    """
    if not tile_entries:
        print("  [WARN] No tile entries to write")
        return

    # 计算整体范围和网格尺寸
    min_col = min(e.col for e in tile_entries)
    max_col = max(e.col for e in tile_entries)
    min_row = min(e.row for e in tile_entries)
    max_row = max(e.row for e in tile_entries)

    all_bounds = [e.geo_bounds for e in tile_entries]
    total_min_lon = min(b[0] for b in all_bounds)
    total_min_lat = min(b[1] for b in all_bounds)
    total_max_lon = max(b[2] for b in all_bounds)
    total_max_lat = max(b[3] for b in all_bounds)

    num_cols = max_col - min_col + 1
    num_rows = max_row - min_row + 1

    epsg = 32600 + utm_zone if northern else 32700 + utm_zone
    projection = f"UTM{utm_zone}{'N' if northern else 'S'}"

    manifest = {
        "projection": projection,
        "epsg": epsg,
        "origin_lon": origin_lon,
        "origin_lat": origin_lat,
        "tile_size_m": tile_size_m,
        "num_cols": num_cols,
        "num_rows": num_rows,
        "total_bounds": {
            "min_lon": total_min_lon,
            "min_lat": total_min_lat,
            "max_lon": total_max_lon,
            "max_lat": total_max_lat,
        },
        "tiles": [entry.to_dict() for entry in tile_entries],
    }

    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    total_features = sum(e.feature_count for e in tile_entries)
    print(f"  Wrote manifest: {len(tile_entries)} tiles, {total_features} total features")
    print(f"  Grid: {num_cols} x {num_rows}, projection: {projection} (EPSG:{epsg})")
