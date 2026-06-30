"""
Georeferencing helpers for the satellite-ground prototype.

A "surface tile" is anchored to the real world by its south-west (SW) corner in
WGS84 plus an extent in metres.  We keep two affine geotransforms:

  * geotransform_utm : pixel (col,row) -> UTM easting/northing (metric, precise)
  * geotransform_geo : pixel (col,row) -> lon/lat (approximate, for quick lookup)

The mesh is emitted in a local ENU frame (metres, origin at the SW corner) so the
UE side can place it with GISCoordinate.SetOrigin(origin_lon, origin_lat) +
GeoToWorld(), exactly like the existing vector content.  This mirrors
Tools/GISPreprocess/projection.py so the demo stays standalone but consistent
with FTileManifest / GISCoordinate on the C++ side.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import List, Tuple

# --- WGS84 ellipsoid (identical to GISPreprocess/projection.py) ---------------
WGS84_A = 6378137.0
WGS84_F = 1.0 / 298.257223563
WGS84_E2 = 2 * WGS84_F - WGS84_F ** 2
WGS84_E_PRIME2 = WGS84_E2 / (1 - WGS84_E2)

UTM_K0 = 0.9996
UTM_FE = 500000.0
UTM_FN_SOUTH = 10000000.0


def utm_zone_from_lon(longitude: float) -> int:
    return int((longitude + 180.0) / 6.0) + 1


def wgs84_to_utm(lon: float, lat: float, zone: int | None = None):
    """WGS84 -> (easting, northing, zone, northern). Mirrors projection.py."""
    if zone is None:
        zone = utm_zone_from_lon(lon)
    lat_rad = math.radians(lat)
    lon_rad = math.radians(lon)
    lon0 = math.radians((zone - 1) * 6 - 180 + 3)
    dl = lon_rad - lon0

    sin_lat = math.sin(lat_rad)
    cos_lat = math.cos(lat_rad)
    tan_lat = math.tan(lat_rad)

    n = WGS84_A / math.sqrt(1 - WGS84_E2 * sin_lat ** 2)
    t = tan_lat ** 2
    c = WGS84_E_PRIME2 * cos_lat ** 2
    a = dl * cos_lat

    m = WGS84_A * (
        (1 - WGS84_E2 / 4 - 3 * WGS84_E2 ** 2 / 64 - 5 * WGS84_E2 ** 3 / 256) * lat_rad
        - (3 * WGS84_E2 / 8 + 3 * WGS84_E2 ** 2 / 32 + 45 * WGS84_E2 ** 3 / 1024) * math.sin(2 * lat_rad)
        + (15 * WGS84_E2 ** 2 / 256 + 45 * WGS84_E2 ** 3 / 1024) * math.sin(4 * lat_rad)
        - (35 * WGS84_E2 ** 3 / 3072) * math.sin(6 * lat_rad)
    )
    easting = UTM_K0 * n * (
        a + (1 - t + c) * a ** 3 / 6
        + (5 - 18 * t + t ** 2 + 72 * c - 58 * WGS84_E_PRIME2) * a ** 5 / 120
    ) + UTM_FE
    northing = UTM_K0 * (
        m + n * tan_lat * (
            a ** 2 / 2
            + (5 - t + 9 * c + 4 * c ** 2) * a ** 4 / 24
            + (61 - 58 * t + t ** 2 + 600 * c - 330 * WGS84_E_PRIME2) * a ** 6 / 720
        )
    )
    northern = lat >= 0
    if not northern:
        northing += UTM_FN_SOUTH
    return easting, northing, zone, northern


def utm_to_wgs84(easting: float, northing: float, zone: int, northern: bool = True):
    """UTM -> (lon, lat). Mirrors projection.py."""
    x = easting - UTM_FE
    y = northing if northern else northing - UTM_FN_SOUTH

    m = y / UTM_K0
    mu = m / (WGS84_A * (1 - WGS84_E2 / 4 - 3 * WGS84_E2 ** 2 / 64 - 5 * WGS84_E2 ** 3 / 256))
    e1 = (1 - math.sqrt(1 - WGS84_E2)) / (1 + math.sqrt(1 - WGS84_E2))

    phi = (mu
           + (3 * e1 / 2 - 27 * e1 ** 3 / 32) * math.sin(2 * mu)
           + (21 * e1 ** 2 / 16 - 55 * e1 ** 4 / 32) * math.sin(4 * mu)
           + (151 * e1 ** 3 / 96) * math.sin(6 * mu))

    sin_phi = math.sin(phi)
    cos_phi = math.cos(phi)
    tan_phi = math.tan(phi)

    n = WGS84_A / math.sqrt(1 - WGS84_E2 * sin_phi ** 2)
    t = tan_phi ** 2
    c = WGS84_E_PRIME2 * cos_phi ** 2
    r = WGS84_A * (1 - WGS84_E2) / (1 - WGS84_E2 * sin_phi ** 2) ** 1.5
    d = x / (n * UTM_K0)

    lat = phi - (n * tan_phi / r) * (
        d ** 2 / 2
        - (5 + 3 * t + 10 * c - 4 * c ** 2 - 9 * WGS84_E_PRIME2) * d ** 4 / 24
        + (61 + 90 * t + 298 * c + 45 * t ** 2 - 252 * WGS84_E_PRIME2 - 3 * c ** 2) * d ** 6 / 720
    )
    lon0 = math.radians((zone - 1) * 6 - 180 + 3)
    lon = lon0 + (
        d
        - (1 + 2 * t + c) * d ** 3 / 6
        + (5 - 2 * c + 28 * t - 3 * c ** 2 + 8 * WGS84_E_PRIME2 + 24 * t ** 2) * d ** 5 / 120
    ) / cos_phi
    return math.degrees(lon), math.degrees(lat)


@dataclass
class TileGeo:
    """Geo-anchor + affine transforms for one square surface tile.

    The tile is defined by its SW corner (origin_lon/lat) and a side length in
    metres.  `px` is the *source* pixel side; texture products (albedo, splat...)
    may have a different pixel side and carry their own metres-per-pixel.
    """
    origin_lon: float
    origin_lat: float
    size_m: float
    px: int

    utm_zone: int = field(init=False)
    northern: bool = field(init=False)
    epsg: int = field(init=False)
    easting0: float = field(init=False)   # UTM easting of SW corner
    northing0: float = field(init=False)  # UTM northing of SW corner
    min_lon: float = field(init=False)
    min_lat: float = field(init=False)
    max_lon: float = field(init=False)
    max_lat: float = field(init=False)

    def __post_init__(self):
        e0, n0, zone, northern = wgs84_to_utm(self.origin_lon, self.origin_lat)
        self.utm_zone = zone
        self.northern = northern
        self.epsg = (32600 if northern else 32700) + zone
        self.easting0 = e0
        self.northing0 = n0
        # Four corners in UTM -> back to lon/lat for an axis-aligned geo bbox.
        corners_utm = [
            (e0, n0),
            (e0 + self.size_m, n0),
            (e0, n0 + self.size_m),
            (e0 + self.size_m, n0 + self.size_m),
        ]
        lons, lats = [], []
        for e, n in corners_utm:
            lon, lat = utm_to_wgs84(e, n, zone, northern)
            lons.append(lon)
            lats.append(lat)
        self.min_lon, self.max_lon = min(lons), max(lons)
        self.min_lat, self.max_lat = min(lats), max(lats)

    def mpp(self, pixel_side: int) -> float:
        """Metres per pixel for a product with `pixel_side` pixels per edge."""
        return self.size_m / float(pixel_side)

    def geotransform_utm(self, pixel_side: int) -> List[float]:
        """GDAL-order affine: pixel(col,row, top-left origin) -> UTM(E,N)."""
        m = self.mpp(pixel_side)
        north_top = self.northing0 + self.size_m
        return [self.easting0, m, 0.0, north_top, 0.0, -m]

    def geotransform_geo(self, pixel_side: int) -> List[float]:
        """Approximate affine: pixel(col,row) -> lon/lat (small-tile linearisation)."""
        dlon = (self.max_lon - self.min_lon) / pixel_side
        dlat = (self.max_lat - self.min_lat) / pixel_side
        return [self.min_lon, dlon, 0.0, self.max_lat, 0.0, -dlat]

    def pixel_to_utm(self, col: float, row: float, pixel_side: int) -> Tuple[float, float]:
        gt = self.geotransform_utm(pixel_side)
        return (gt[0] + col * gt[1] + row * gt[2],
                gt[3] + col * gt[4] + row * gt[5])

    def pixel_to_geo(self, col: float, row: float, pixel_side: int) -> Tuple[float, float]:
        e, n = self.pixel_to_utm(col, row, pixel_side)
        return utm_to_wgs84(e, n, self.utm_zone, self.northern)

    def local_enu(self, col: float, row: float, pixel_side: int) -> Tuple[float, float]:
        """Pixel -> local ENU metres (east, north) from the SW corner.

        Row 0 is the north edge, so north = size_m - row*mpp.
        """
        m = self.mpp(pixel_side)
        return (col * m, self.size_m - row * m)

    def as_dict(self) -> dict:
        return {
            "crs": "WGS84",
            "origin_lon": self.origin_lon,
            "origin_lat": self.origin_lat,
            "min_lon": self.min_lon,
            "min_lat": self.min_lat,
            "max_lon": self.max_lon,
            "max_lat": self.max_lat,
            "utm_zone": self.utm_zone,
            "epsg": self.epsg,
            "northern": self.northern,
            "size_m": self.size_m,
            "easting0": self.easting0,
            "northing0": self.northing0,
        }
