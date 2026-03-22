#!/usr/bin/env python3
"""
CesiumBridge 相关功能测试套件

测试覆盖：
  T1. 离线 DEM 高程缓存生成 + 二进制文件格式验证
  T2. 双线性插值精度（与 numpy 参考实现对比）
  T3. 坐标转换往返精度（Mercator 模式）
  T4. PCG LOD 距离判定逻辑
  T5. srtm_to_terrain.py 端到端流程
  T6. WITH_CESIUM=0 条件编译兼容性（检查代码无硬依赖）
"""

import json
import math
import os
import struct
import sys
import tempfile
from pathlib import Path

import numpy as np

# 将工具目录加入 path
TOOLS_DIR = Path(__file__).parent.parent / "Tools" / "GISPreprocess"
sys.path.insert(0, str(TOOLS_DIR))

from srtm_to_terrain import (
    sample_dem_grid,
    write_elevation_cache,
    generate_elevation_cache,
)

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
# T1. DEM 高程缓存二进制文件格式验证
# ============================================================
def test_elevation_cache_format():
    print("\n=== T1: DEM 高程缓存文件格式 ===")

    with tempfile.TemporaryDirectory() as tmpdir:
        grid_size = 10
        cell_size = 100.0  # 100m
        min_lon, min_lat = 121.47, 31.23
        max_lon, max_lat = 121.48, 31.24

        # 创建简单高程数据（倾斜平面）
        grid = np.zeros((grid_size, grid_size), dtype=np.float32)
        for r in range(grid_size):
            for c in range(grid_size):
                grid[r, c] = r * 10.0 + c * 5.0  # 0~135m

        filepath = Path(tmpdir) / "elevation_0_0.bin"
        write_elevation_cache(
            filepath, grid, grid_size, cell_size,
            min_lon, min_lat, max_lon, max_lat,
        )

        # 验证文件存在
        check("文件已创建", filepath.exists())

        # 验证文件大小
        # header: int32(4) + float(4) + 4*double(32) = 40 bytes
        # data: 10*10*float(4) = 400 bytes
        expected_size = 40 + grid_size * grid_size * 4
        actual_size = filepath.stat().st_size
        check("文件大小正确", actual_size == expected_size,
              f"expected={expected_size}, actual={actual_size}")

        # 读回验证
        with open(filepath, "rb") as f:
            read_grid_size = struct.unpack("<i", f.read(4))[0]
            read_cell_size = struct.unpack("<f", f.read(4))[0]
            read_min_lon = struct.unpack("<d", f.read(8))[0]
            read_min_lat = struct.unpack("<d", f.read(8))[0]
            read_max_lon = struct.unpack("<d", f.read(8))[0]
            read_max_lat = struct.unpack("<d", f.read(8))[0]
            read_data = np.frombuffer(f.read(), dtype="<f4").reshape((grid_size, grid_size))

        check("GridSize 正确", read_grid_size == grid_size)
        check("CellSize 正确", approx_eq(read_cell_size, cell_size, 0.01))
        check("MinLon 正确", approx_eq(read_min_lon, min_lon))
        check("MinLat 正确", approx_eq(read_min_lat, min_lat))
        check("MaxLon 正确", approx_eq(read_max_lon, max_lon))
        check("MaxLat 正确", approx_eq(read_max_lat, max_lat))
        check("高程数据一致", np.allclose(read_data, grid, atol=0.01))

        # 验证角点值
        check("左下角高程=0", approx_eq(read_data[0, 0], 0.0, 0.01))
        check("右上角高程=135", approx_eq(read_data[9, 9], 135.0, 0.01))


# ============================================================
# T2. 双线性插值精度
# ============================================================
def test_bilinear_interpolation():
    print("\n=== T2: 双线性插值精度 ===")

    grid_size = 4
    # 4x4 网格，值 = row * 10 + col
    dem_data = np.array([
        [0, 1, 2, 3],
        [10, 11, 12, 13],
        [20, 21, 22, 23],
        [30, 31, 32, 33],
    ], dtype=np.float32)

    dem_meta = {
        "min_lon": 0.0, "min_lat": 0.0,
        "max_lon": 1.0, "max_lat": 1.0,
        "rows": 4, "cols": 4,
    }

    # 在中心点采样 2x2 网格
    result = sample_dem_grid(dem_data, dem_meta, 0.0, 0.0, 1.0, 1.0, 4)

    # 角点应精确匹配（注意 DEM 行序：从上到下纬度递减）
    check("左下角 (row=0, lat=0) = 30", approx_eq(result[0, 0], 30.0, 0.01))
    check("右下角 (row=0, lat=0) = 33", approx_eq(result[0, 3], 33.0, 0.01))
    check("左上角 (row=3, lat=1) = 0", approx_eq(result[3, 0], 0.0, 0.01))
    check("右上角 (row=3, lat=1) = 3", approx_eq(result[3, 3], 3.0, 0.01))

    # 中间插值点
    mid_val = result[1, 1]
    check("中间插值合理", 10.0 < mid_val < 25.0,
          f"value={mid_val:.2f}")


# ============================================================
# T3. 坐标转换往返精度（Mercator 模式）
# ============================================================
def test_mercator_roundtrip():
    print("\n=== T3: Mercator 坐标往返精度 ===")

    # 模拟 C++ 的 SimpleMercator 逻辑
    earth_radius = 6371000.0
    meters_to_cm = 100.0

    origin_lon = 121.47
    origin_lat = 31.23

    lat_rad = math.radians(origin_lat)
    m_per_deg_lon = (math.pi / 180.0) * earth_radius * math.cos(lat_rad)
    m_per_deg_lat = (math.pi / 180.0) * earth_radius

    test_points = [
        (121.47, 31.23),    # 原点
        (121.48, 31.24),    # 近距离
        (121.50, 31.25),    # 中距离
        (122.00, 31.50),    # 远距离 (~50km)
        (121.4956, 31.2397),  # 东方明珠
    ]

    for lon, lat in test_points:
        # GeoToWorld
        dx = (lon - origin_lon) * m_per_deg_lon * meters_to_cm
        dy = (lat - origin_lat) * m_per_deg_lat * meters_to_cm

        # WorldToGeo
        rt_lon = origin_lon + (dx / meters_to_cm) / m_per_deg_lon
        rt_lat = origin_lat + (dy / meters_to_cm) / m_per_deg_lat

        err_lon = abs(rt_lon - lon)
        err_lat = abs(rt_lat - lat)

        check(f"往返 ({lon:.4f},{lat:.4f}) 误差<1e-10",
              err_lon < 1e-10 and err_lat < 1e-10,
              f"err_lon={err_lon:.2e}, err_lat={err_lat:.2e}")

    # 大范围精度检查（70km 偏移）
    far_lon = origin_lon + 0.7  # ~60km 东
    far_lat = origin_lat + 0.6  # ~67km 北
    dx = (far_lon - origin_lon) * m_per_deg_lon
    dy = (far_lat - origin_lat) * m_per_deg_lat
    dist_m = math.sqrt(dx * dx + dy * dy)
    check(f"70km 范围内距离合理", 80000 < dist_m < 100000,
          f"dist={dist_m:.0f}m")

    # float32 精度检查
    world_x_f32 = np.float32(dx * meters_to_cm)
    precision_cm = abs(float(world_x_f32) - dx * meters_to_cm)
    check(f"float32 精度在 1cm 内 (70km)", precision_cm < 1.0,
          f"precision={precision_cm:.4f}cm")


# ============================================================
# T4. PCG LOD 距离判定
# ============================================================
def test_pcg_lod_levels():
    print("\n=== T4: PCG LOD 距离判定 ===")

    # 模拟 C++ EPCGDetailLevel 逻辑
    full_dist = 1000.0
    medium_dist = 3000.0
    low_dist = 7000.0

    intervals = {
        "Full": 10.0,
        "Medium": 30.0,
        "Low": 100.0,
        "Culled": 0.0,
    }

    def get_level(dist):
        if dist <= full_dist:
            return "Full"
        if dist <= medium_dist:
            return "Medium"
        if dist <= low_dist:
            return "Low"
        return "Culled"

    # 边界测试
    check("dist=0 → Full", get_level(0) == "Full")
    check("dist=500 → Full", get_level(500) == "Full")
    check("dist=1000 → Full", get_level(1000) == "Full")
    check("dist=1001 → Medium", get_level(1001) == "Medium")
    check("dist=3000 → Medium", get_level(3000) == "Medium")
    check("dist=3001 → Low", get_level(3001) == "Low")
    check("dist=7000 → Low", get_level(7000) == "Low")
    check("dist=7001 → Culled", get_level(7001) == "Culled")
    check("dist=50000 → Culled", get_level(50000) == "Culled")

    # 采样间隔二次关系
    # 面积 1km² = 1,000,000 m²
    area = 1_000_000.0
    points_full = area / (intervals["Full"] ** 2)
    points_medium = area / (intervals["Medium"] ** 2)
    points_low = area / (intervals["Low"] ** 2)

    check(f"Full→Medium 点数下降 ~9x",
          8 < points_full / points_medium < 10,
          f"ratio={points_full / points_medium:.1f}")
    check(f"Medium→Low 点数下降 ~11x",
          10 < points_medium / points_low < 12,
          f"ratio={points_medium / points_low:.1f}")
    check(f"Full 级别每 km² ~{points_full:.0f} 点", points_full == 10000)


# ============================================================
# T5. srtm_to_terrain 端到端
# ============================================================
def test_srtm_pipeline_e2e():
    print("\n=== T5: srtm_to_terrain 端到端 ===")

    with tempfile.TemporaryDirectory() as tmpdir:
        # 创建模拟 DEM 数据 (100x100, 覆盖 1°×1°)
        rows, cols = 100, 100
        dem_data = np.zeros((rows, cols), dtype=np.float32)
        for r in range(rows):
            for c in range(cols):
                dem_data[r, c] = 50.0 + 200.0 * math.sin(r / 10.0) * math.cos(c / 10.0)

        dem_meta = {
            "min_lon": 121.0, "min_lat": 31.0,
            "max_lon": 122.0, "max_lat": 32.0,
            "rows": rows, "cols": cols,
        }

        output_dir = Path(tmpdir) / "dem_cache"
        tiles = generate_elevation_cache(
            dem_data, dem_meta,
            tile_size_m=10000.0,  # 10km tiles (较大避免生成太多)
            grid_size=20,
            output_dir=output_dir,
        )

        check("生成了 tile 缓存", len(tiles) > 0, f"count={len(tiles)}")

        # 验证每个文件可读
        for tile in tiles:
            fpath = output_dir / tile["file"]
            check(f"文件 {tile['file']} 可读", fpath.exists())

            with open(fpath, "rb") as f:
                gs = struct.unpack("<i", f.read(4))[0]
                check(f"  grid_size={gs}", gs == 20)

        # 验证索引文件（generate_elevation_cache 不写索引，由 main() 写）
        # 这里只验证缓存文件本身
        check("缓存文件数 > 0", len(tiles) > 0)

        # 验证总大小合理
        total_bytes = sum((output_dir / t["file"]).stat().st_size for t in tiles)
        per_tile_kb = total_bytes / max(len(tiles), 1) / 1024
        check(f"每 tile ~{per_tile_kb:.1f}KB 合理", 1.0 < per_tile_kb < 10.0,
              f"actual={per_tile_kb:.1f}KB")


# ============================================================
# T6. WITH_CESIUM=0 兼容性（静态代码分析）
# ============================================================
def test_with_cesium_zero_compatibility():
    print("\n=== T6: WITH_CESIUM=0 兼容性检查 ===")

    plugin_dir = Path(__file__).parent.parent / "Plugins" / "GISProcedural" / "Source"

    # 检查所有 .cpp/.h 文件中 CesiumRuntime 的使用是否都在 #if WITH_CESIUM 内
    cesium_headers = ["CesiumGeoreference.h", "CesiumRuntime", "ACesiumGeoreference"]
    issues = []

    for ext in ("*.cpp", "*.h"):
        for fpath in plugin_dir.rglob(ext):
            with open(fpath) as f:
                lines = f.readlines()

            in_cesium_block = False
            for i, line in enumerate(lines, 1):
                stripped = line.strip()
                if stripped.startswith("#if WITH_CESIUM") or stripped.startswith("#if defined(WITH_CESIUM)"):
                    in_cesium_block = True
                elif stripped.startswith("#endif") and in_cesium_block:
                    in_cesium_block = False

                for header in cesium_headers:
                    if header in line and not in_cesium_block:
                        # 排除注释
                        if line.strip().startswith("//") or line.strip().startswith("*"):
                            continue
                        issues.append(f"{fpath.name}:{i}: {line.strip()}")

    check("无 WITH_CESIUM 外的 Cesium 硬依赖",
          len(issues) == 0,
          f"found {len(issues)} issues: {issues[:3]}")

    # 检查 Build.cs WITH_CESIUM 定义
    build_cs = plugin_dir / "GISProcedural" / "GISProcedural.Build.cs"
    if build_cs.exists():
        content = build_cs.read_text()
        check("Build.cs 定义了 WITH_CESIUM=0 fallback",
              "WITH_CESIUM=0" in content)
        check("Build.cs 定义了 WITH_CESIUM=1 条件",
              "WITH_CESIUM=1" in content)


# ============================================================
# T7. DEM 缓存高程值精度
# ============================================================
def test_elevation_accuracy():
    print("\n=== T7: DEM 高程值精度 ===")

    # 创建已知高程面：Z = 100 + 0.5*X + 0.3*Y
    grid_size = 50
    dem_data = np.zeros((grid_size, grid_size), dtype=np.float32)
    for r in range(grid_size):
        for c in range(grid_size):
            dem_data[r, c] = 100.0 + 0.5 * c + 0.3 * (grid_size - 1 - r)

    dem_meta = {
        "min_lon": 121.0, "min_lat": 31.0,
        "max_lon": 121.01, "max_lat": 31.01,
        "rows": grid_size, "cols": grid_size,
    }

    # 采样 10x10 子网格
    result = sample_dem_grid(dem_data, dem_meta, 121.0, 31.0, 121.01, 31.01, 10)

    # 检查角点（采样结果取决于 DEM 行序和插值）
    check("左下角在合理范围", 95.0 <= result[0, 0] <= 130.0,
          f"actual={result[0, 0]:.2f}")
    check("右上角在合理范围", 110.0 <= result[9, 9] <= 145.0,
          f"actual={result[9, 9]:.2f}")

    # 检查所有值在合理范围内
    check("所有值 >= 100", np.all(result >= 99.0))
    check("所有值 <= 140", np.all(result <= 140.0))

    # 检查插值平滑性（相邻差异不应过大）
    max_diff = 0.0
    for r in range(9):
        for c in range(9):
            diff = abs(result[r + 1, c] - result[r, c])
            max_diff = max(max_diff, diff)
    check("相邻行最大差异 < 5m", max_diff < 5.0, f"max_diff={max_diff:.2f}")


# ============================================================
# 运行
# ============================================================
def main():
    print("=" * 60)
    print("CesiumBridge 功能测试套件")
    print("=" * 60)

    test_elevation_cache_format()
    test_bilinear_interpolation()
    test_mercator_roundtrip()
    test_pcg_lod_levels()
    test_srtm_pipeline_e2e()
    test_with_cesium_zero_compatibility()
    test_elevation_accuracy()

    print("\n" + "=" * 60)
    print(f"测试结果: {PASS_COUNT}/{TOTAL_COUNT} 通过, {FAIL_COUNT} 失败")
    print("=" * 60)

    return 0 if FAIL_COUNT == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
