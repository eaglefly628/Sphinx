"""
DEM 裁切器
将大幅 DEM (GeoTIFF/HGT) 按瓦片范围裁切为 PNG 高度图。
"""
import struct
from pathlib import Path
from typing import List

try:
    from osgeo import gdal, osr
    HAS_GDAL = True
except ImportError:
    HAS_GDAL = False


class DEMCropper:
    """将 DEM 数据裁切到每个 tile 的范围"""

    def crop_to_tiles(self, dem_files: List[Path], tile_entries, tiles_dir: Path):
        """
        对每个 tile entry，从 DEM 源中裁切出对应区域。

        Args:
            dem_files: DEM 源文件列表 (GeoTIFF / HGT)
            tile_entries: TileEntry 列表
            tiles_dir: 输出瓦片根目录
        """
        if not dem_files:
            print("  No DEM files to process")
            return

        if not HAS_GDAL:
            print("  [WARN] GDAL not available, writing stub DEM files")
            self._write_stub_dem(tile_entries, tiles_dir)
            return

        # 打开所有 DEM 源构建虚拟镶嵌
        print(f"  Opening {len(dem_files)} DEM sources...")
        vrt_path = str(tiles_dir / "_temp_dem.vrt")
        gdal.BuildVRT(vrt_path, [str(f) for f in dem_files])
        src_ds = gdal.Open(vrt_path)

        if not src_ds:
            print("  [WARN] Failed to open DEM VRT, writing stubs")
            self._write_stub_dem(tile_entries, tiles_dir)
            return

        cropped_count = 0
        for entry in tile_entries:
            min_lon, min_lat, max_lon, max_lat = entry.geo_bounds
            tile_dir = tiles_dir / f"tile_{entry.col}_{entry.row}"
            tile_dir.mkdir(parents=True, exist_ok=True)

            out_path = tile_dir / "dem.png"

            # gdal.Translate 裁切 + 转 PNG16
            try:
                ds = gdal.Translate(
                    str(out_path),
                    src_ds,
                    format="PNG",
                    outputType=gdal.GDT_UInt16,
                    projWin=[min_lon, max_lat, max_lon, min_lat],  # ulx, uly, lrx, lry
                    width=256,
                    height=256,
                )
                if ds:
                    ds = None  # 关闭写入
                    entry.dem_rel_path = f"tiles/tile_{entry.col}_{entry.row}/dem.png"
                    cropped_count += 1
            except Exception as e:
                print(f"    [WARN] DEM crop failed for tile_{entry.col}_{entry.row}: {e}")

        src_ds = None
        # 清理临时 VRT
        vrt_file = Path(vrt_path)
        if vrt_file.exists():
            vrt_file.unlink()

        print(f"  Cropped DEM for {cropped_count}/{len(tile_entries)} tiles")

    def _write_stub_dem(self, tile_entries, tiles_dir: Path):
        """无 GDAL 时写入占位 DEM 文件（16bit PNG，全零）"""
        for entry in tile_entries:
            tile_dir = tiles_dir / f"tile_{entry.col}_{entry.row}"
            tile_dir.mkdir(parents=True, exist_ok=True)

            # 最小化 PNG：写一个 1x1 像素 16bit 灰度 PNG 占位
            out_path = tile_dir / "dem.png"
            _write_minimal_png(out_path)
            entry.dem_rel_path = f"tiles/tile_{entry.col}_{entry.row}/dem.png"

        print(f"  Wrote {len(tile_entries)} stub DEM files (GDAL required for real DEM)")


def _write_minimal_png(path: Path):
    """写一个极简 1x1 PNG（占位用）"""
    import zlib
    # 1x1 16bit grayscale PNG
    width, height, bit_depth, color_type = 1, 1, 16, 0
    raw_data = b'\x00\x00\x00'  # filter byte + 2 bytes pixel
    compressed = zlib.compress(raw_data)

    def chunk(chunk_type, data):
        c = chunk_type + data
        crc = zlib.crc32(c) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + c + struct.pack(">I", crc)

    with open(path, "wb") as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        ihdr_data = struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, 0)
        f.write(chunk(b'IHDR', ihdr_data))
        f.write(chunk(b'IDAT', compressed))
        f.write(chunk(b'IEND', b''))
