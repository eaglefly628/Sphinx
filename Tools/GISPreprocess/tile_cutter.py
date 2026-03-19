"""
矢量数据瓦片切割器
将 GeoJSON 文件按 tile 网格切分，输出每个 tile 的 GeoJSON 文件。
"""
import json
import math
from pathlib import Path
from typing import List, Dict, Any

from projection import (
    wgs84_to_utm, utm_to_wgs84, meters_per_degree_lon, meters_per_degree_lat,
    tile_bounds_to_geo,
)


class TileEntry:
    """单个瓦片的元数据，对应 UE5 FTileEntry"""

    def __init__(self, col: int, row: int):
        self.col = col
        self.row = row
        self.geo_bounds = (0.0, 0.0, 0.0, 0.0)  # min_lon, min_lat, max_lon, max_lat
        self.geojson_rel_path = ""
        self.dem_rel_path = ""
        self.landcover_rel_path = ""
        self.feature_count = 0

    def to_dict(self) -> dict:
        return {
            "x": self.col,
            "y": self.row,
            "geojson": self.geojson_rel_path,
            "dem": self.dem_rel_path,
            "landcover": self.landcover_rel_path,
            "bounds_geo": {
                "min_lon": self.geo_bounds[0],
                "min_lat": self.geo_bounds[1],
                "max_lon": self.geo_bounds[2],
                "max_lat": self.geo_bounds[3],
            },
            "polygon_count": self.feature_count,
        }


class TileCutter:
    def __init__(self, tile_size_m: float, origin_lon: float, origin_lat: float,
                 utm_zone: int, northern_hemisphere: bool):
        self.tile_size_m = tile_size_m
        self.origin_lon = origin_lon
        self.origin_lat = origin_lat
        self.utm_zone = utm_zone
        self.northern = northern_hemisphere

        # 原点的 UTM 坐标
        self.origin_e, self.origin_n, _, _ = wgs84_to_utm(
            origin_lon, origin_lat, utm_zone)

    def _lonlat_to_tile(self, lon: float, lat: float) -> tuple:
        """经纬度 → 瓦片 (col, row)"""
        e, n, _, _ = wgs84_to_utm(lon, lat, self.utm_zone)
        col = math.floor((e - self.origin_e) / self.tile_size_m)
        row = math.floor((n - self.origin_n) / self.tile_size_m)
        return col, row

    def _feature_centroid(self, geometry: dict) -> tuple:
        """计算要素几何的近似质心"""
        geom_type = geometry.get("type", "")
        coords = geometry.get("coordinates", [])

        if geom_type == "Point":
            return coords[0], coords[1]
        elif geom_type == "MultiPoint":
            if not coords:
                return 0, 0
            avg_lon = sum(c[0] for c in coords) / len(coords)
            avg_lat = sum(c[1] for c in coords) / len(coords)
            return avg_lon, avg_lat
        elif geom_type == "LineString":
            if not coords:
                return 0, 0
            avg_lon = sum(c[0] for c in coords) / len(coords)
            avg_lat = sum(c[1] for c in coords) / len(coords)
            return avg_lon, avg_lat
        elif geom_type == "Polygon":
            ring = coords[0] if coords else []
            if not ring:
                return 0, 0
            avg_lon = sum(c[0] for c in ring) / len(ring)
            avg_lat = sum(c[1] for c in ring) / len(ring)
            return avg_lon, avg_lat
        elif geom_type == "MultiPolygon":
            all_pts = []
            for polygon in coords:
                if polygon:
                    all_pts.extend(polygon[0])
            if not all_pts:
                return 0, 0
            avg_lon = sum(c[0] for c in all_pts) / len(all_pts)
            avg_lat = sum(c[1] for c in all_pts) / len(all_pts)
            return avg_lon, avg_lat
        elif geom_type == "MultiLineString":
            all_pts = []
            for line in coords:
                all_pts.extend(line)
            if not all_pts:
                return 0, 0
            avg_lon = sum(c[0] for c in all_pts) / len(all_pts)
            avg_lat = sum(c[1] for c in all_pts) / len(all_pts)
            return avg_lon, avg_lat

        return 0, 0

    def _feature_bbox(self, geometry: dict):
        """计算要素几何的 bounding box"""
        geom_type = geometry.get("type", "")
        coords = geometry.get("coordinates", [])

        all_points = []

        if geom_type == "Point":
            all_points = [coords]
        elif geom_type == "MultiPoint":
            all_points = coords
        elif geom_type == "LineString":
            all_points = coords
        elif geom_type == "MultiLineString":
            for line in coords:
                all_points.extend(line)
        elif geom_type == "Polygon":
            for ring in coords:
                all_points.extend(ring)
        elif geom_type == "MultiPolygon":
            for polygon in coords:
                for ring in polygon:
                    all_points.extend(ring)

        if not all_points:
            return None

        min_lon = min(p[0] for p in all_points)
        max_lon = max(p[0] for p in all_points)
        min_lat = min(p[1] for p in all_points)
        max_lat = max(p[1] for p in all_points)

        return min_lon, min_lat, max_lon, max_lat

    def cut_geojson(self, geojson_files: List[Path],
                    tiles_dir: Path) -> List[TileEntry]:
        """
        将 GeoJSON 文件按瓦片网格切分。

        策略：
        - 以要素质心所在 tile 为主归属
        - 跨 tile 边界的要素在属性中标记 "clipped": true
        - 每个要素分配全局唯一 feature_id 用于去重
        """
        # 收集所有要素
        all_features: List[Dict[str, Any]] = []
        for gj_path in geojson_files:
            with open(gj_path, "r", encoding="utf-8") as f:
                data = json.load(f)
            features = data.get("features", [])
            all_features.extend(features)

        print(f"  Total features to distribute: {len(all_features)}")

        # 按 tile 分桶
        tile_buckets: Dict[tuple, List[Dict]] = {}
        global_id = 0

        for feature in all_features:
            geometry = feature.get("geometry")
            if not geometry:
                continue

            # 分配全局 ID
            if "properties" not in feature:
                feature["properties"] = {}
            feature["properties"]["sphinx_feature_id"] = global_id
            global_id += 1

            # 质心决定主瓦片
            centroid_lon, centroid_lat = self._feature_centroid(geometry)
            primary_col, primary_row = self._lonlat_to_tile(centroid_lon, centroid_lat)

            # 检查 bbox 是否跨越 tile 边界
            bbox = self._feature_bbox(geometry)
            if bbox:
                min_col, min_row = self._lonlat_to_tile(bbox[0], bbox[1])
                max_col, max_row = self._lonlat_to_tile(bbox[2], bbox[3])

                crosses_boundary = (min_col != max_col or min_row != max_row)
                feature["properties"]["clipped"] = crosses_boundary

                # 要素分配到所有覆盖的 tile（主 tile 和交叠 tile）
                for c in range(min_col, max_col + 1):
                    for r in range(min_row, max_row + 1):
                        key = (c, r)
                        if key not in tile_buckets:
                            tile_buckets[key] = []
                        tile_buckets[key].append(feature)
            else:
                feature["properties"]["clipped"] = False
                key = (primary_col, primary_row)
                if key not in tile_buckets:
                    tile_buckets[key] = []
                tile_buckets[key].append(feature)

        # 写出每个 tile 的 GeoJSON
        tile_entries = []
        for (col, row), features in sorted(tile_buckets.items()):
            tile_dir = tiles_dir / f"tile_{col}_{row}"
            tile_dir.mkdir(parents=True, exist_ok=True)

            geojson_path = tile_dir / "osm.geojson"
            geojson_data = {
                "type": "FeatureCollection",
                "features": features,
            }

            with open(geojson_path, "w", encoding="utf-8") as f:
                json.dump(geojson_data, f, ensure_ascii=False)

            entry = TileEntry(col, row)
            entry.geo_bounds = tile_bounds_to_geo(
                col, row, self.tile_size_m, self.origin_lon, self.origin_lat)
            entry.geojson_rel_path = f"tiles/tile_{col}_{row}/osm.geojson"
            entry.feature_count = len(features)

            tile_entries.append(entry)
            print(f"    tile_{col}_{row}: {len(features)} features")

        return tile_entries
