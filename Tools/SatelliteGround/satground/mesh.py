"""
Mesh stage: build a georeferenced ground mesh as a regular grid and export it as
OBJ + GLB for the standalone viewer / external inspection.

Frame: centred ENU metres.  x = east, y = up (height), z = -north, with the
origin at the tile centre.  This is purely for visualisation -- the UE side will
rebuild the surface procedurally from the data structure (SW-corner geo anchor +
height field) via GISCoordinate, so it stays aligned with the existing vector
content.  The mesh meta records the geo anchor so nothing is lost.

UV convention: u = col/N (west->east), v = row/N (north->south), with row 0 at
the north edge -- matching the texture rasters.  Viewer samples with flipY=false.
"""
from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import pygltflib


def _sample_height(height: np.ndarray, fr: float, fc: float) -> float:
    """Bilinear sample height at fractional (row, col)."""
    h, w = height.shape
    r0 = int(np.clip(np.floor(fr), 0, h - 1)); r1 = min(r0 + 1, h - 1)
    c0 = int(np.clip(np.floor(fc), 0, w - 1)); c1 = min(c0 + 1, w - 1)
    dr, dc = fr - r0, fc - c0
    top = height[r0, c0] * (1 - dc) + height[r0, c1] * dc
    bot = height[r1, c0] * (1 - dc) + height[r1, c1] * dc
    return float(top * (1 - dr) + bot * dr)


def build_grid_mesh(height: np.ndarray, geo, grid_n: int = 128):
    """Return dict of float32/uint32 arrays + meta for a (grid_n x grid_n) mesh."""
    h, w = height.shape
    size_m = geo.size_m
    half = size_m / 2.0
    n = grid_n
    verts = (n + 1) * (n + 1)

    pos = np.zeros((verts, 3), dtype=np.float32)
    uv = np.zeros((verts, 2), dtype=np.float32)
    grid_z = np.zeros((n + 1, n + 1), dtype=np.float32)  # height per grid node

    for i in range(n + 1):          # row, north(0) -> south(n)
        fr = i / n * (h - 1)
        north = (1.0 - i / n) * size_m
        for j in range(n + 1):      # col, west(0) -> east(n)
            fc = j / n * (w - 1)
            east = j / n * size_m
            z = _sample_height(height, fr, fc)
            grid_z[i, j] = z
            vi = i * (n + 1) + j
            pos[vi] = (east - half, z, -(north - half))
            uv[vi] = (j / n, i / n)

    # Vertex normals from the grid height (metric spacing).
    # Surface P(east,north) = (east, z, -north); normal = (-dz/deast, 1, dz/dnorth).
    # With dzdy = d z/di / mpp, dzdx = d z/dj / mpp and north decreasing in i:
    #   dz/deast = dzdx ; dz/dnorth = -dzdy  => normal = (-dzdx, 1, -dzdy).
    mpp_grid = size_m / n
    dzdy, dzdx = np.gradient(grid_z, mpp_grid)
    nrm = np.dstack([-dzdx, np.ones_like(grid_z), -dzdy])
    nrm = nrm.reshape(-1, 3)
    nrm /= np.linalg.norm(nrm, axis=1, keepdims=True)
    normals = nrm.astype(np.float32)

    # Triangle indices (two per quad).
    idx = []
    for i in range(n):
        for j in range(n):
            a = i * (n + 1) + j
            b = a + 1
            c = a + (n + 1)
            d = c + 1
            idx.extend([a, c, b, b, c, d])
    indices = np.array(idx, dtype=np.uint32)

    cy, cx = h / 2.0, w / 2.0
    center_lon, center_lat = geo.pixel_to_geo(cx, cy, h)
    meta = {
        "frame": "ENU_centered_m",
        "x_axis": "east", "y_axis": "up", "z_axis": "-north",
        "grid_n": n,
        "vertices": verts,
        "triangles": int(len(indices) // 3),
        "size_m": size_m,
        "origin_lon": geo.origin_lon, "origin_lat": geo.origin_lat,
        "center_lon": center_lon, "center_lat": center_lat,
        "z_min_m": float(grid_z.min()), "z_max_m": float(grid_z.max()),
        "uv": "u=col/N (E), v=row/N (N->S), flipY=false",
    }
    return {"pos": pos, "uv": uv, "normals": normals, "indices": indices}, meta


def write_obj(mesh: dict, path: Path):
    pos, uv, nrm, idx = mesh["pos"], mesh["uv"], mesh["normals"], mesh["indices"]
    lines = ["# Sphinx satellite-ground surface mesh (ENU centred metres)"]
    for p in pos:
        lines.append(f"v {p[0]:.4f} {p[1]:.4f} {p[2]:.4f}")
    for t in uv:
        lines.append(f"vt {t[0]:.6f} {1.0 - t[1]:.6f}")  # OBJ v origin bottom-left
    for nv in nrm:
        lines.append(f"vn {nv[0]:.4f} {nv[1]:.4f} {nv[2]:.4f}")
    for k in range(0, len(idx), 3):
        a, b, c = idx[k] + 1, idx[k + 1] + 1, idx[k + 2] + 1
        lines.append(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}")
    Path(path).write_text("\n".join(lines))


def write_glb(mesh: dict, path: Path):
    """Geometry-only GLB (positions/normals/uv/indices). Viewer adds the shader."""
    pos = mesh["pos"].astype(np.float32)
    nrm = mesh["normals"].astype(np.float32)
    uv = mesh["uv"].astype(np.float32)
    idx = mesh["indices"].astype(np.uint32)

    pos_b = pos.tobytes()
    nrm_b = nrm.tobytes()
    uv_b = uv.tobytes()
    idx_b = idx.tobytes()

    def pad(b):
        return b + b"\x00" * ((4 - len(b) % 4) % 4)

    blob = pad(pos_b) + pad(nrm_b) + pad(uv_b) + pad(idx_b)
    off_pos = 0
    off_nrm = len(pad(pos_b))
    off_uv = off_nrm + len(pad(nrm_b))
    off_idx = off_uv + len(pad(uv_b))

    gltf = pygltflib.GLTF2(
        scene=0,
        scenes=[pygltflib.Scene(nodes=[0])],
        nodes=[pygltflib.Node(mesh=0)],
        meshes=[pygltflib.Mesh(primitives=[pygltflib.Primitive(
            attributes=pygltflib.Attributes(POSITION=0, NORMAL=1, TEXCOORD_0=2),
            indices=3, mode=4)])],
        accessors=[
            pygltflib.Accessor(bufferView=0, componentType=pygltflib.FLOAT,
                               count=len(pos), type="VEC3",
                               min=pos.min(axis=0).tolist(),
                               max=pos.max(axis=0).tolist()),
            pygltflib.Accessor(bufferView=1, componentType=pygltflib.FLOAT,
                               count=len(nrm), type="VEC3"),
            pygltflib.Accessor(bufferView=2, componentType=pygltflib.FLOAT,
                               count=len(uv), type="VEC2"),
            pygltflib.Accessor(bufferView=3, componentType=pygltflib.UNSIGNED_INT,
                               count=len(idx), type="SCALAR"),
        ],
        bufferViews=[
            pygltflib.BufferView(buffer=0, byteOffset=off_pos, byteLength=len(pos_b),
                                 target=pygltflib.ARRAY_BUFFER),
            pygltflib.BufferView(buffer=0, byteOffset=off_nrm, byteLength=len(nrm_b),
                                 target=pygltflib.ARRAY_BUFFER),
            pygltflib.BufferView(buffer=0, byteOffset=off_uv, byteLength=len(uv_b),
                                 target=pygltflib.ARRAY_BUFFER),
            pygltflib.BufferView(buffer=0, byteOffset=off_idx, byteLength=len(idx_b),
                                 target=pygltflib.ELEMENT_ARRAY_BUFFER),
        ],
        buffers=[pygltflib.Buffer(byteLength=len(blob))],
    )
    gltf.set_binary_blob(blob)
    gltf.save_binary(str(path))
