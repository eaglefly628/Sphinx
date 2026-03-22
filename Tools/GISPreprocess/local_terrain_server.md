# 本地 Cesium 地形服务配置

## 方案 A：cesium-terrain-server（推荐）

```bash
# 安装
go install github.com/geo-data/cesium-terrain-server@latest

# 启动（假设 Quantized Mesh tiles 在 ./terrain/ 目录）
cesium-terrain-server -dir ./terrain -port 8080
```

Cesium ion asset URL 设为：`http://localhost:8080/tilesets/terrain`

## 方案 B：nginx 静态文件服务

```nginx
server {
    listen 8080;
    root /path/to/terrain/tiles;

    location / {
        add_header Access-Control-Allow-Origin *;
        add_header Content-Encoding gzip;
        types {
            application/vnd.quantized-mesh terrain;
            application/json json;
        }
    }
}
```

## 方案 C：Python 简易服务（开发用）

```bash
cd terrain/
python -m http.server 8080 --bind 0.0.0.0
```

## UE5 Cesium 配置

在 Cesium3DTileset Actor 中：
- Source: URL
- URL: `http://localhost:8080/layer.json`

或在 CesiumIonRasterOverlay 中：
- 使用自定义 terrain provider URL

## 高程缓存用法

高程缓存文件（`elevation_X_Y.bin`）放在 UE5 项目的 `Content/GISData/Region_01/dem_cache/` 目录。

GISWorldBuilder 的 CesiumTiled 模式会自动扫描并加载。
