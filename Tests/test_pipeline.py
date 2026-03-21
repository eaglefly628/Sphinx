#!/usr/bin/env python3
"""
GISProcedural 插件 自动化测试套件
用虚拟数据验证核心管线逻辑的正确性。

测试覆盖:
  T1. UTM 投影正反变换精度
  T2. 瓦片切割 - 要素分桶 + 跨瓦片标记 + sphinx_feature_id
  T3. Manifest 生成 + 字段完整性
  T4. Validate 验证器 - 正常/异常情况
  T5. LandCover 多数投票算法（Python 侧模拟 C++ 逻辑）
  T6. 空间索引查询逻辑（Python 侧模拟 C++ grid 逻辑）
  T7. 端到端管线：虚拟 GeoJSON → 切割 → manifest → validate
"""
import json
import math
import os
import sys
import shutil
import tempfile
from pathlib import Path

# 将工具目录加入 path
TOOLS_DIR = Path(__file__).parent.parent / "Tools" / "GISPreprocess"
sys.path.insert(0, str(TOOLS_DIR))

from projection import (
    wgs84_to_utm, utm_to_wgs84, utm_zone_from_lon,
    meters_per_degree_lon, meters_per_degree_lat, tile_bounds_to_geo,
)
from tile_cutter import TileCutter, TileEntry
from manifest_writer import write_manifest
from validate import validate_output

# ============================================================
# 工具
# ============================================================
PASS_COUNT = 0
FAIL_COUNT = 0
TOTAL_COUNT = 0


def check(name: str, condition: bool, detail: str = ""):
    global PASS_COUNT, FAIL_COUNT, TOTAL_COUNT
    TOTAL_COUNT += 1
    if condition:
        PASS_COUNT += 1
        print(f"  [PASS] {name}")
    else:
        FAIL_COUNT += 1
        msg = f"  [FAIL] {name}"
        if detail:
            msg += f" — {detail}"
        print(msg)


def approx_eq(a, b, tol=1e-6):
    return abs(a - b) < tol


# ============================================================
# T1. UTM 投影正反变换
# ============================================================
def test_utm_projection():
    print("\n" + "=" * 60)
    print("T1. UTM 投影正反变换")
    print("=" * 60)

    test_points = [
        # (name, lon, lat, expected_zone)
        ("上海", 121.4737, 31.2304, 51),
        ("东京", 139.6917, 35.6895, 54),
        ("伦敦", -0.1278, 51.5074, 30),
        ("纽约", -74.0060, 40.7128, 18),
        ("悉尼 (南半球)", 151.2093, -33.8688, 56),
        ("赤道/本初子午线", 0.0, 0.0, 31),
        ("北极附近", 15.0, 89.0, 33),
    ]

    for name, lon, lat, expected_zone in test_points:
        # 正向变换
        zone = utm_zone_from_lon(lon)
        check(f"{name}: UTM zone={zone}", zone == expected_zone,
              f"expected {expected_zone}, got {zone}")

        e, n, z, northern = wgs84_to_utm(lon, lat)
        check(f"{name}: easting>0 northing>=0",
              e > 0 and n >= 0,
              f"e={e:.1f}, n={n:.1f}")

        # 反向变换
        lon2, lat2 = utm_to_wgs84(e, n, z, northern)
        check(f"{name}: 往返精度 <0.001°",
              approx_eq(lon, lon2, 0.001) and approx_eq(lat, lat2, 0.001),
              f"orig=({lon:.6f},{lat:.6f}) → back=({lon2:.6f},{lat2:.6f})")

    # 特殊: meters_per_degree
    m_lon = meters_per_degree_lon(31.23)
    m_lat = meters_per_degree_lat()
    check("meters_per_degree_lon(31.23) ∈ [90000, 100000]",
          90000 < m_lon < 100000, f"got {m_lon:.1f}")
    check("meters_per_degree_lat ≈ 111320",
          approx_eq(m_lat, 111320, 200), f"got {m_lat:.1f}")

    # tile_bounds_to_geo 正确性
    min_lon, min_lat, max_lon, max_lat = tile_bounds_to_geo(
        0, 0, 1000.0, 121.0, 31.0)
    check("tile_bounds_to_geo: 1km tile 角度跨度合理",
          0.008 < (max_lon - min_lon) < 0.015 and 0.008 < (max_lat - min_lat) < 0.010,
          f"dlon={max_lon-min_lon:.6f}, dlat={max_lat-min_lat:.6f}")


# ============================================================
# T2. 瓦片切割逻辑
# ============================================================
def test_tile_cutter():
    print("\n" + "=" * 60)
    print("T2. 瓦片切割逻辑")
    print("=" * 60)

    tmpdir = Path(tempfile.mkdtemp(prefix="sphinx_test_"))

    try:
        # 构造虚拟 GeoJSON: 上海附近的 6 个要素
        origin_lon, origin_lat = 121.47, 31.23
        zone = utm_zone_from_lon(origin_lon)
        tile_size = 1000.0  # 1km

        # 6 个要素: 3 个在同一 tile, 2 个在相邻 tile, 1 个跨 tile
        m_per_lon = meters_per_degree_lon(origin_lat)
        m_per_lat = meters_per_degree_lat()
        d_lon_100m = 100.0 / m_per_lon
        d_lat_100m = 100.0 / m_per_lat
        d_lon_1km = tile_size / m_per_lon

        features = []

        # 要素 0-2: tile(0,0) 内，小建筑
        for i in range(3):
            offset = i * d_lon_100m
            features.append({
                "type": "Feature",
                "geometry": {
                    "type": "Polygon",
                    "coordinates": [[[origin_lon + offset, origin_lat + d_lat_100m],
                                     [origin_lon + offset + d_lon_100m * 0.5, origin_lat + d_lat_100m],
                                     [origin_lon + offset + d_lon_100m * 0.5, origin_lat],
                                     [origin_lon + offset, origin_lat],
                                     [origin_lon + offset, origin_lat + d_lat_100m]]]
                },
                "properties": {"building": "yes", "name": f"building_{i}"}
            })

        # 要素 3-4: tile(1,0) 内
        for i in range(2):
            base_lon = origin_lon + d_lon_1km + i * d_lon_100m
            features.append({
                "type": "Feature",
                "geometry": {
                    "type": "Polygon",
                    "coordinates": [[[base_lon, origin_lat],
                                     [base_lon + d_lon_100m, origin_lat],
                                     [base_lon + d_lon_100m, origin_lat + d_lat_100m],
                                     [base_lon, origin_lat + d_lat_100m],
                                     [base_lon, origin_lat]]]
                },
                "properties": {"landuse": "residential", "name": f"area_{i}"}
            })

        # 要素 5: 跨 tile 边界的道路 (LineString)
        features.append({
            "type": "Feature",
            "geometry": {
                "type": "LineString",
                "coordinates": [
                    [origin_lon + d_lon_1km * 0.8, origin_lat],  # tile(0,0) 内
                    [origin_lon + d_lon_1km * 1.2, origin_lat],  # tile(1,0) 内
                ]
            },
            "properties": {"highway": "primary", "name": "cross_road"}
        })

        geojson_data = {"type": "FeatureCollection", "features": features}
        gj_path = tmpdir / "test_input.geojson"
        with open(gj_path, "w") as f:
            json.dump(geojson_data, f)

        tiles_dir = tmpdir / "tiles"
        tiles_dir.mkdir()

        # 执行切割
        cutter = TileCutter(tile_size, origin_lon, origin_lat, zone, True)
        entries = cutter.cut_geojson([gj_path], tiles_dir)

        check("生成的 tile 数量 >= 2", len(entries) >= 2,
              f"got {len(entries)} tiles")

        # 验证 tile 坐标
        tile_coords = {(e.col, e.row) for e in entries}
        check("存在 tile(0,0)", (0, 0) in tile_coords)
        check("存在 tile(1,0)", (1, 0) in tile_coords)

        # 验证要素数量
        total_features_in_tiles = sum(e.feature_count for e in entries)
        check("总要素数 >= 原始数（跨 tile 要素会重复）",
              total_features_in_tiles >= 6,
              f"原始=6, tiles 合计={total_features_in_tiles}")

        # 验证 sphinx_feature_id 分配
        all_ids = set()
        for entry in entries:
            gj_rel = entry.geojson_rel_path
            # 实际路径需要调整
            gj_file = tiles_dir / f"tile_{entry.col}_{entry.row}" / "osm.geojson"
            if gj_file.exists():
                with open(gj_file) as f:
                    data = json.load(f)
                for feat in data["features"]:
                    fid = feat["properties"].get("sphinx_feature_id")
                    if fid is not None:
                        all_ids.add(fid)

        check("sphinx_feature_id 覆盖所有原始要素",
              len(all_ids) == 6,
              f"expected 6 unique IDs, got {len(all_ids)}: {sorted(all_ids)}")

        # 验证跨 tile 要素标记
        cross_road_found = False
        for entry in entries:
            gj_file = tiles_dir / f"tile_{entry.col}_{entry.row}" / "osm.geojson"
            if gj_file.exists():
                with open(gj_file) as f:
                    data = json.load(f)
                for feat in data["features"]:
                    if feat["properties"].get("name") == "cross_road":
                        if feat["properties"].get("clipped"):
                            cross_road_found = True

        check("跨 tile 道路标记 clipped=true", cross_road_found)

        # geo_bounds 合理
        for entry in entries:
            b = entry.geo_bounds
            check(f"tile({entry.col},{entry.row}) bounds 有效 (min<max)",
                  b[0] < b[2] and b[1] < b[3],
                  f"bounds={b}")

    finally:
        shutil.rmtree(tmpdir)


# ============================================================
# T3. Manifest 生成 + T4. Validate 验证
# ============================================================
def test_manifest_and_validate():
    print("\n" + "=" * 60)
    print("T3. Manifest 生成 + T4. Validate 验证")
    print("=" * 60)

    tmpdir = Path(tempfile.mkdtemp(prefix="sphinx_test_manifest_"))

    try:
        origin_lon, origin_lat = 121.47, 31.23
        zone = utm_zone_from_lon(origin_lon)
        tile_size = 1024.0

        # 创建虚拟 TileEntry
        entries = []
        tiles_dir = tmpdir / "tiles"
        for col in range(3):
            for row in range(2):
                e = TileEntry(col, row)
                e.geo_bounds = tile_bounds_to_geo(col, row, tile_size, origin_lon, origin_lat)
                e.feature_count = 10 + col * 5 + row * 3

                # 创建对应的 GeoJSON 文件
                tile_dir = tiles_dir / f"tile_{col}_{row}"
                tile_dir.mkdir(parents=True, exist_ok=True)
                gj_path = tile_dir / "osm.geojson"
                gj_data = {
                    "type": "FeatureCollection",
                    "features": [
                        {"type": "Feature",
                         "geometry": {"type": "Point", "coordinates": [origin_lon, origin_lat]},
                         "properties": {"id": i}}
                        for i in range(e.feature_count)
                    ]
                }
                with open(gj_path, "w") as f:
                    json.dump(gj_data, f)

                e.geojson_rel_path = f"tiles/tile_{col}_{row}/osm.geojson"
                entries.append(e)

        # 写 manifest
        manifest_path = tmpdir / "tile_manifest.json"
        write_manifest(manifest_path, entries, tile_size, zone, True, origin_lon, origin_lat)

        check("manifest 文件生成", manifest_path.exists())

        # 读取并验证结构
        with open(manifest_path) as f:
            manifest = json.load(f)

        check("projection 字段", manifest["projection"] == f"UTM{zone}N",
              f"got '{manifest['projection']}'")
        check("EPSG 正确", manifest["epsg"] == 32600 + zone,
              f"got {manifest['epsg']}")
        check("origin_lon 正确", approx_eq(manifest["origin_lon"], origin_lon, 0.001))
        check("origin_lat 正确", approx_eq(manifest["origin_lat"], origin_lat, 0.001))
        check("tile_size_m 正确", manifest["tile_size_m"] == tile_size)
        check("num_cols=3", manifest["num_cols"] == 3)
        check("num_rows=2", manifest["num_rows"] == 2)
        check("tiles 数组长度=6", len(manifest["tiles"]) == 6)

        total_features = sum(t["polygon_count"] for t in manifest["tiles"])
        check("total features = sum(entries)",
              total_features == sum(e.feature_count for e in entries),
              f"expected {sum(e.feature_count for e in entries)}, got {total_features}")

        # 每个 tile 的 bounds_geo 有效
        for tile in manifest["tiles"]:
            b = tile["bounds_geo"]
            check(f"tile({tile['x']},{tile['y']}) bounds valid",
                  b["min_lon"] < b["max_lon"] and b["min_lat"] < b["max_lat"])

        # T4: 运行 validate
        print("\n--- T4. Validate 验证器 ---")
        result = validate_output(tmpdir)
        check("validate_output 通过", result)

        # 故意破坏测试 validate 错误检测
        # 删除一个 tile 文件
        broken_file = tiles_dir / "tile_1_1" / "osm.geojson"
        if broken_file.exists():
            broken_file.unlink()
        result_broken = validate_output(tmpdir)
        check("validate_output 检测到文件缺失", not result_broken)

    finally:
        shutil.rmtree(tmpdir)


# ============================================================
# T5. LandCover 多数投票（模拟 C++ FuseLandCoverData 逻辑）
# ============================================================
def test_landcover_majority_vote():
    print("\n" + "=" * 60)
    print("T5. LandCover 多数投票算法")
    print("=" * 60)

    # ESA WorldCover 类码映射 (与 C++ LandUseClassifier.cpp 一致)
    def worldcover_to_landuse(code):
        mapping = {
            10: "Forest", 20: "Forest", 30: "OpenSpace", 40: "Farmland",
            50: "Residential", 60: "OpenSpace", 70: "OpenSpace",
            80: "Water", 90: "Water", 95: "Forest", 100: "OpenSpace"
        }
        return mapping.get(code, "Unknown")

    def majority_vote(grid, width, height, col_min, col_max, row_min, row_max):
        """模拟 C++ FuseLandCoverData 的多数投票"""
        votes = {}
        total = 0
        for r in range(max(row_min, 0), min(row_max + 1, height)):
            for c in range(max(col_min, 0), min(col_max + 1, width)):
                code = grid[r * width + c]
                landuse = worldcover_to_landuse(code)
                if landuse != "Unknown":
                    votes[landuse] = votes.get(landuse, 0) + 1
                    total += 1

        if total == 0:
            return "Unknown"

        # 水体优先级 (>1/3 → Water)
        water_votes = votes.get("Water", 0)
        if water_votes > total / 3:
            return "Water"

        return max(votes, key=votes.get)

    # 测试 1: 全森林
    grid1 = [10] * 25  # 5x5 全树
    result = majority_vote(grid1, 5, 5, 0, 4, 0, 4)
    check("全森林区域 → Forest", result == "Forest", f"got {result}")

    # 测试 2: 混合区域（农田为主）
    grid2 = [40, 40, 40, 10, 10,
             40, 40, 40, 40, 10,
             40, 40, 50, 50, 10,
             40, 40, 40, 40, 40,
             40, 40, 40, 40, 40]
    result = majority_vote(grid2, 5, 5, 0, 4, 0, 4)
    check("农田为主 → Farmland", result == "Farmland", f"got {result}")

    # 测试 3: 水体优先（>1/3 即判定水体）
    grid3 = [80, 80, 80, 80, 80,  # 5 water
             80, 80, 80, 80, 10,  # 4 water = 9/25 > 1/3
             10, 10, 10, 10, 10,
             10, 10, 10, 10, 10,
             10, 10, 10, 10, 10]
    result = majority_vote(grid3, 5, 5, 0, 4, 0, 4)
    check("水体>1/3 → Water (水体优先)", result == "Water", f"got {result}")

    # 测试 4: 水体不足 1/3（不触发水体优先）
    grid4 = [80, 10, 10, 10, 10,
             10, 10, 10, 10, 10,
             10, 10, 10, 10, 10,
             10, 10, 10, 10, 10,
             10, 10, 10, 10, 10]
    result = majority_vote(grid4, 5, 5, 0, 4, 0, 4)
    check("水体=1/25 → Forest (不触发水体优先)", result == "Forest", f"got {result}")

    # 测试 5: 子区域查询
    grid5 = [10, 10, 40, 40, 40,
             10, 10, 40, 40, 40,
             10, 10, 40, 40, 40,
             50, 50, 50, 50, 50,
             50, 50, 50, 50, 50]
    result_left = majority_vote(grid5, 5, 5, 0, 1, 0, 2)
    result_right = majority_vote(grid5, 5, 5, 2, 4, 0, 2)
    check("左上子区域 → Forest", result_left == "Forest", f"got {result_left}")
    check("右上子区域 → Farmland", result_right == "Farmland", f"got {result_right}")


# ============================================================
# T6. 空间索引查询逻辑（模拟 C++ BuildSpatialIndex + GetPolygonsInWorldBounds）
# ============================================================
def test_spatial_index():
    print("\n" + "=" * 60)
    print("T6. 空间索引查询逻辑")
    print("=" * 60)

    # 模拟 C++ ULandUseMapDataAsset 的网格空间索引
    class SpatialIndex:
        def __init__(self, cell_size):
            self.cell_size = cell_size
            self.grid = {}  # {(cx,cy): [polygon_indices]}

        def build(self, polygon_centers):
            self.grid.clear()
            for i, (x, y) in enumerate(polygon_centers):
                cx = math.floor(x / self.cell_size)
                cy = math.floor(y / self.cell_size)
                key = (cx, cy)
                if key not in self.grid:
                    self.grid[key] = []
                self.grid[key].append(i)

        def query(self, min_x, min_y, max_x, max_y, polygon_centers):
            result = []
            visited = set()
            cx_min = math.floor(min_x / self.cell_size)
            cx_max = math.floor(max_x / self.cell_size)
            cy_min = math.floor(min_y / self.cell_size)
            cy_max = math.floor(max_y / self.cell_size)

            for cx in range(cx_min, cx_max + 1):
                for cy in range(cy_min, cy_max + 1):
                    indices = self.grid.get((cx, cy), [])
                    for idx in indices:
                        if idx not in visited:
                            visited.add(idx)
                            px, py = polygon_centers[idx]
                            if min_x <= px <= max_x and min_y <= py <= max_y:
                                result.append(idx)
            return result

    # 模拟: 100 个多边形分布在 10km x 10km 区域
    import random
    random.seed(42)
    centers = [(random.uniform(0, 1000000), random.uniform(0, 1000000))
               for _ in range(100)]  # 单位: cm (1000000cm = 10km)

    # 暴力查询（Ground truth）
    def brute_force_query(min_x, min_y, max_x, max_y):
        return [i for i, (x, y) in enumerate(centers)
                if min_x <= x <= max_x and min_y <= y <= max_y]

    # 构建空间索引（500m = 50000cm cell）
    idx = SpatialIndex(50000.0)
    idx.build(centers)

    check(f"空间索引 cell 数量 > 0", len(idx.grid) > 0,
          f"got {len(idx.grid)} cells")

    # 测试几个不同的查询区域
    test_bounds = [
        ("全区域", 0, 0, 1000000, 1000000),
        ("中心 2km", 400000, 400000, 600000, 600000),
        ("左下角 1km", 0, 0, 100000, 100000),
        ("右上角 500m", 950000, 950000, 1000000, 1000000),
        ("极窄条带", 0, 499000, 1000000, 501000),
    ]

    for name, x0, y0, x1, y1 in test_bounds:
        expected = sorted(brute_force_query(x0, y0, x1, y1))
        actual = sorted(idx.query(x0, y0, x1, y1, centers))
        check(f"查询 '{name}': 结果一致 (expected={len(expected)}, actual={len(actual)})",
              expected == actual,
              f"expected={expected[:5]}..., actual={actual[:5]}...")


# ============================================================
# T7. 端到端管线测试
# ============================================================
def test_end_to_end():
    print("\n" + "=" * 60)
    print("T7. 端到端管线测试")
    print("=" * 60)

    tmpdir = Path(tempfile.mkdtemp(prefix="sphinx_e2e_"))

    try:
        origin_lon, origin_lat = 121.47, 31.23
        zone = utm_zone_from_lon(origin_lon)
        tile_size = 500.0

        m_per_lon = meters_per_degree_lon(origin_lat)
        m_per_lat = meters_per_degree_lat()

        # 构造虚拟区域: 2x2 瓦片 (1km x 1km), 每个瓦片有 2-5 个要素
        features = []
        for col in range(2):
            for row in range(2):
                n_feat = 2 + col + row
                for i in range(n_feat):
                    cx = origin_lon + (col * tile_size + 100 + i * 80) / m_per_lon
                    cy = origin_lat + (row * tile_size + 100 + i * 80) / m_per_lat
                    sz = 50.0 / m_per_lon
                    features.append({
                        "type": "Feature",
                        "geometry": {
                            "type": "Polygon",
                            "coordinates": [[[cx, cy], [cx+sz, cy], [cx+sz, cy+sz], [cx, cy+sz], [cx, cy]]]
                        },
                        "properties": {
                            "building": "yes",
                            "name": f"bld_{col}_{row}_{i}",
                        }
                    })

        input_dir = tmpdir / "input"
        input_dir.mkdir()
        output_dir = tmpdir / "output"
        output_dir.mkdir()

        gj_path = input_dir / "test.geojson"
        with open(gj_path, "w") as f:
            json.dump({"type": "FeatureCollection", "features": features}, f)

        total_input = len(features)
        print(f"  Input: {total_input} features in 2x2 tile grid")

        # Step 1: 切割
        tiles_dir = output_dir / "tiles"
        tiles_dir.mkdir()
        cutter = TileCutter(tile_size, origin_lon, origin_lat, zone, True)
        entries = cutter.cut_geojson([gj_path], tiles_dir)

        check("Step 1 切割: 生成 >= 4 tiles", len(entries) >= 4,
              f"got {len(entries)}")

        # Step 2: 写 manifest
        manifest_path = output_dir / "tile_manifest.json"
        write_manifest(manifest_path, entries, tile_size, zone, True, origin_lon, origin_lat)
        check("Step 2 Manifest: 文件生成", manifest_path.exists())

        # Step 3: 验证
        valid = validate_output(output_dir)
        check("Step 3 Validate: 全部通过", valid)

        # Step 4: 读回 manifest 验证 UE5 解析兼容性
        with open(manifest_path) as f:
            m = json.load(f)

        # 模拟 UE5 TileManifest::ParseFromJson 的解析
        check("UE5 兼容: projection 以 UTM 开头", m["projection"].startswith("UTM"))
        check("UE5 兼容: 所有 tile 有 bounds_geo",
              all("bounds_geo" in t for t in m["tiles"]))
        check("UE5 兼容: 所有 tile 有 polygon_count",
              all("polygon_count" in t for t in m["tiles"]))

        # 模拟 TiledFileProvider 去重逻辑
        all_feature_ids = set()
        duplicate_count = 0
        for entry in entries:
            gj_file = tiles_dir / f"tile_{entry.col}_{entry.row}" / "osm.geojson"
            if gj_file.exists():
                with open(gj_file) as f:
                    data = json.load(f)
                for feat in data["features"]:
                    fid = feat["properties"].get("sphinx_feature_id")
                    if fid is not None:
                        if fid in all_feature_ids:
                            duplicate_count += 1
                        else:
                            all_feature_ids.add(fid)

        check(f"去重: {len(all_feature_ids)} 唯一要素 = 原始 {total_input}",
              len(all_feature_ids) == total_input)
        print(f"  (跨 tile 重复出现 {duplicate_count} 次，已通过 sphinx_feature_id 去重)")

    finally:
        shutil.rmtree(tmpdir)


# ============================================================
# Main
# ============================================================
if __name__ == "__main__":
    print("=" * 60)
    print("GISProcedural 自动化测试套件")
    print("=" * 60)

    test_utm_projection()
    test_tile_cutter()
    test_manifest_and_validate()
    test_landcover_majority_vote()
    test_spatial_index()
    test_end_to_end()

    print("\n" + "=" * 60)
    print(f"测试结果: {PASS_COUNT}/{TOTAL_COUNT} 通过, {FAIL_COUNT} 失败")
    print("=" * 60)

    sys.exit(0 if FAIL_COUNT == 0 else 1)
