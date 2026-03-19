"""
WGS84 ↔ UTM 投影转换
与 UE5 侧 GISCoordinate.cpp 的 GeoToUTM/UTMToGeo 实现保持一致。
"""
import math

# WGS84 椭球参数
WGS84_A = 6378137.0
WGS84_F = 1.0 / 298.257223563
WGS84_E2 = 2 * WGS84_F - WGS84_F ** 2
WGS84_E_PRIME2 = WGS84_E2 / (1 - WGS84_E2)

# UTM 参数
UTM_K0 = 0.9996
UTM_FE = 500000.0  # False Easting
UTM_FN_SOUTH = 10000000.0  # False Northing (南半球)


def utm_zone_from_lon(longitude: float) -> int:
    """经度 → UTM 区号 (1-60)"""
    return int((longitude + 180.0) / 6.0) + 1


def wgs84_to_utm(lon: float, lat: float, zone: int = None):
    """
    WGS84 经纬度 → UTM 东距/北距

    Returns:
        (easting, northing, zone, northern_hemisphere)
    """
    if zone is None:
        zone = utm_zone_from_lon(lon)

    lat_rad = math.radians(lat)
    lon_rad = math.radians(lon)

    # 中央子午线
    lon0 = math.radians((zone - 1) * 6 - 180 + 3)
    dl = lon_rad - lon0

    sin_lat = math.sin(lat_rad)
    cos_lat = math.cos(lat_rad)
    tan_lat = math.tan(lat_rad)

    n = WGS84_A / math.sqrt(1 - WGS84_E2 * sin_lat ** 2)
    t = tan_lat ** 2
    c = WGS84_E_PRIME2 * cos_lat ** 2
    a_coeff = dl * cos_lat

    # 子午线弧长
    m = WGS84_A * (
        (1 - WGS84_E2 / 4 - 3 * WGS84_E2 ** 2 / 64 - 5 * WGS84_E2 ** 3 / 256) * lat_rad
        - (3 * WGS84_E2 / 8 + 3 * WGS84_E2 ** 2 / 32 + 45 * WGS84_E2 ** 3 / 1024) * math.sin(2 * lat_rad)
        + (15 * WGS84_E2 ** 2 / 256 + 45 * WGS84_E2 ** 3 / 1024) * math.sin(4 * lat_rad)
        - (35 * WGS84_E2 ** 3 / 3072) * math.sin(6 * lat_rad)
    )

    easting = UTM_K0 * n * (
        a_coeff
        + (1 - t + c) * a_coeff ** 3 / 6
        + (5 - 18 * t + t ** 2 + 72 * c - 58 * WGS84_E_PRIME2) * a_coeff ** 5 / 120
    ) + UTM_FE

    northing = UTM_K0 * (
        m + n * tan_lat * (
            a_coeff ** 2 / 2
            + (5 - t + 9 * c + 4 * c ** 2) * a_coeff ** 4 / 24
            + (61 - 58 * t + t ** 2 + 600 * c - 330 * WGS84_E_PRIME2) * a_coeff ** 6 / 720
        )
    )

    northern = lat >= 0
    if not northern:
        northing += UTM_FN_SOUTH

    return easting, northing, zone, northern


def utm_to_wgs84(easting: float, northing: float, zone: int, northern: bool = True):
    """
    UTM → WGS84 经纬度

    Returns:
        (longitude, latitude)
    """
    x = easting - UTM_FE
    y = northing
    if not northern:
        y -= UTM_FN_SOUTH

    m = y / UTM_K0
    mu = m / (WGS84_A * (1 - WGS84_E2 / 4 - 3 * WGS84_E2 ** 2 / 64 - 5 * WGS84_E2 ** 3 / 256))

    e1 = (1 - math.sqrt(1 - WGS84_E2)) / (1 + math.sqrt(1 - WGS84_E2))

    phi1 = mu + (
        (3 * e1 / 2 - 27 * e1 ** 3 / 32) * math.sin(2 * mu)
        + (21 * e1 ** 2 / 16 - 55 * e1 ** 4 / 32) * math.sin(4 * mu)
        + (151 * e1 ** 3 / 96) * math.sin(6 * mu)
        + (1097 * e1 ** 4 / 512) * math.sin(8 * mu)
    )

    sin_phi1 = math.sin(phi1)
    cos_phi1 = math.cos(phi1)
    tan_phi1 = math.tan(phi1)

    n1 = WGS84_A / math.sqrt(1 - WGS84_E2 * sin_phi1 ** 2)
    t1 = tan_phi1 ** 2
    c1 = WGS84_E_PRIME2 * cos_phi1 ** 2
    r1 = WGS84_A * (1 - WGS84_E2) / ((1 - WGS84_E2 * sin_phi1 ** 2) ** 1.5)
    d = x / (n1 * UTM_K0)

    lat = phi1 - (n1 * tan_phi1 / r1) * (
        d ** 2 / 2
        - (5 + 3 * t1 + 10 * c1 - 4 * c1 ** 2 - 9 * WGS84_E_PRIME2) * d ** 4 / 24
        + (61 + 90 * t1 + 298 * c1 + 45 * t1 ** 2 - 252 * WGS84_E_PRIME2 - 3 * c1 ** 2) * d ** 6 / 720
    )

    lon = ((zone - 1) * 6 - 180 + 3) + math.degrees(
        (d
         - (1 + 2 * t1 + c1) * d ** 3 / 6
         + (5 - 2 * c1 + 28 * t1 - 3 * c1 ** 2 + 8 * WGS84_E_PRIME2 + 24 * t1 ** 2) * d ** 5 / 120)
        / cos_phi1
    )

    lat = math.degrees(lat)

    return lon, lat


def meters_per_degree_lon(lat: float) -> float:
    """在给定纬度处，1 经度对应的米数"""
    return math.cos(math.radians(lat)) * math.pi * WGS84_A / 180.0


def meters_per_degree_lat() -> float:
    """1 纬度对应的近似米数（约111320m）"""
    return math.pi * WGS84_A / 180.0


def tile_bounds_to_geo(col: int, row: int, tile_size_m: float,
                       origin_lon: float, origin_lat: float):
    """
    瓦片 (col, row) → 地理矩形 (min_lon, min_lat, max_lon, max_lat)

    使用原点处的局部线性近似。
    """
    m_per_deg_lon = meters_per_degree_lon(origin_lat)
    m_per_deg_lat_val = meters_per_degree_lat()

    min_lon = origin_lon + (col * tile_size_m) / m_per_deg_lon
    max_lon = origin_lon + ((col + 1) * tile_size_m) / m_per_deg_lon
    min_lat = origin_lat + (row * tile_size_m) / m_per_deg_lat_val
    max_lat = origin_lat + ((row + 1) * tile_size_m) / m_per_deg_lat_val

    return min_lon, min_lat, max_lon, max_lat
