"""
输出验证器
检查预处理结果的完整性和正确性。
"""
import json
from pathlib import Path


def validate_output(output_dir: Path) -> bool:
    """
    验证预处理输出目录的完整性。

    检查项:
    1. tile_manifest.json 存在且可解析
    2. 每个 tile 的文件存在
    3. GeoJSON 文件可解析且有 features
    4. 地理范围合理性
    5. 无重复 tile 坐标

    Returns:
        True if all checks pass
    """
    output_dir = Path(output_dir)
    issues = []

    # 1. manifest 存在
    manifest_path = output_dir / "tile_manifest.json"
    if not manifest_path.exists():
        issues.append("tile_manifest.json not found")
        _print_issues(issues)
        return False

    try:
        with open(manifest_path, "r", encoding="utf-8") as f:
            manifest = json.load(f)
    except json.JSONDecodeError as e:
        issues.append(f"tile_manifest.json is invalid JSON: {e}")
        _print_issues(issues)
        return False

    # 2. manifest 必要字段
    required_fields = ["projection", "origin_lon", "origin_lat", "tile_size_m", "tiles"]
    for field in required_fields:
        if field not in manifest:
            issues.append(f"Missing field in manifest: {field}")

    tiles = manifest.get("tiles", [])
    if not tiles:
        issues.append("No tiles in manifest")

    # 3. tile 文件存在性
    seen_coords = set()
    for i, tile in enumerate(tiles):
        coord = (tile.get("x", 0), tile.get("y", 0))

        # 重复检查
        if coord in seen_coords:
            issues.append(f"Duplicate tile coordinate: {coord}")
        seen_coords.add(coord)

        # GeoJSON 文件
        geojson_rel = tile.get("geojson", "")
        if geojson_rel:
            geojson_path = output_dir / geojson_rel
            if not geojson_path.exists():
                issues.append(f"Tile {coord}: GeoJSON not found: {geojson_rel}")
            else:
                try:
                    with open(geojson_path, "r", encoding="utf-8") as f:
                        gj = json.load(f)
                    feat_count = len(gj.get("features", []))
                    declared_count = tile.get("polygon_count", 0)
                    if feat_count != declared_count:
                        issues.append(
                            f"Tile {coord}: feature count mismatch "
                            f"(file={feat_count}, manifest={declared_count})")
                except json.JSONDecodeError:
                    issues.append(f"Tile {coord}: GeoJSON is invalid JSON")

        # DEM 文件
        dem_rel = tile.get("dem", "")
        if dem_rel:
            dem_path = output_dir / dem_rel
            if not dem_path.exists():
                issues.append(f"Tile {coord}: DEM not found: {dem_rel}")

        # LandCover 文件
        lc_rel = tile.get("landcover", "")
        if lc_rel:
            lc_path = output_dir / lc_rel
            if not lc_path.exists():
                issues.append(f"Tile {coord}: LandCover not found: {lc_rel}")

        # 4. 地理范围合理性
        bounds = tile.get("bounds_geo", {})
        min_lon = bounds.get("min_lon", 0)
        max_lon = bounds.get("max_lon", 0)
        min_lat = bounds.get("min_lat", 0)
        max_lat = bounds.get("max_lat", 0)

        if min_lon >= max_lon:
            issues.append(f"Tile {coord}: invalid lon range [{min_lon}, {max_lon}]")
        if min_lat >= max_lat:
            issues.append(f"Tile {coord}: invalid lat range [{min_lat}, {max_lat}]")

        if not (-180 <= min_lon <= 180 and -180 <= max_lon <= 180):
            issues.append(f"Tile {coord}: longitude out of range")
        if not (-90 <= min_lat <= 90 and -90 <= max_lat <= 90):
            issues.append(f"Tile {coord}: latitude out of range")

    # 5. 输出
    _print_issues(issues)
    total_features = sum(t.get("polygon_count", 0) for t in tiles)
    print(f"  Summary: {len(tiles)} tiles, {total_features} total features, {len(issues)} issues")

    return len(issues) == 0


def _print_issues(issues):
    for issue in issues:
        print(f"  [ISSUE] {issue}")
