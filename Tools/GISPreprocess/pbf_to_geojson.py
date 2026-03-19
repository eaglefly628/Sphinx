"""
PBF → GeoJSON 转换器
使用 osmium 将 OpenStreetMap PBF 文件转换为 GeoJSON。
"""
import subprocess
import sys
from pathlib import Path


def pbf_to_geojson(pbf_path: Path, output_path: Path):
    """
    将 .osm.pbf 文件转换为 GeoJSON。
    优先使用 osmium，回退到 ogr2ogr。
    """
    pbf_path = Path(pbf_path)
    output_path = Path(output_path)

    if not pbf_path.exists():
        raise FileNotFoundError(f"PBF file not found: {pbf_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    # 尝试 osmium export
    if _try_osmium(pbf_path, output_path):
        return

    # 回退到 ogr2ogr (GDAL)
    if _try_ogr2ogr(pbf_path, output_path):
        return

    print(f"[ERROR] Cannot convert PBF: neither 'osmium' nor 'ogr2ogr' found.")
    print("  Install osmium-tool: apt install osmium-tool / brew install osmium-tool")
    print("  Or install GDAL:     apt install gdal-bin / brew install gdal")
    sys.exit(1)


def _try_osmium(pbf_path: Path, output_path: Path) -> bool:
    """尝试用 osmium export 转换"""
    try:
        result = subprocess.run(
            ["osmium", "export", "-f", "geojson", "-o", str(output_path), str(pbf_path)],
            capture_output=True, text=True, timeout=600
        )
        if result.returncode == 0:
            print(f"  osmium: {pbf_path.name} → {output_path.name}")
            return True
        print(f"  osmium failed: {result.stderr.strip()}")
        return False
    except FileNotFoundError:
        return False
    except subprocess.TimeoutExpired:
        print(f"  osmium: timeout processing {pbf_path.name}")
        return False


def _try_ogr2ogr(pbf_path: Path, output_path: Path) -> bool:
    """尝试用 ogr2ogr 转换"""
    try:
        result = subprocess.run(
            ["ogr2ogr", "-f", "GeoJSON", str(output_path), str(pbf_path),
             "-progress", "multipolygons"],
            capture_output=True, text=True, timeout=600
        )
        if result.returncode == 0:
            print(f"  ogr2ogr: {pbf_path.name} → {output_path.name}")
            return True
        print(f"  ogr2ogr failed: {result.stderr.strip()}")
        return False
    except FileNotFoundError:
        return False
    except subprocess.TimeoutExpired:
        print(f"  ogr2ogr: timeout processing {pbf_path.name}")
        return False
