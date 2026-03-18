// DEMParser.cpp - DEM 瓦片解析器实现
#include "DEM/DEMParser.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Modules/ModuleManager.h"

bool UDEMParser::LoadTile(const FString& FilePath)
{
    EDEMFormat ActualFormat = Format;
    if (ActualFormat == EDEMFormat::Auto)
    {
        ActualFormat = DetectFormat(FilePath);
    }

    FDEMTileInfo Info;
    TArray<float> Data;
    bool bSuccess = false;

    switch (ActualFormat)
    {
        case EDEMFormat::GeoTIFF:
            bSuccess = ParseGeoTIFF(FilePath, Info, Data);
            break;
        case EDEMFormat::HeightmapPNG:
            bSuccess = ParseHeightmapPNG(FilePath, Info, Data);
            break;
        case EDEMFormat::HeightmapRAW:
            bSuccess = ParseHeightmapRAW(FilePath, Info, Data);
            break;
        default:
            UE_LOG(LogTemp, Error, TEXT("DEMParser: Unknown format for %s"), *FilePath);
            return false;
    }

    if (bSuccess)
    {
        Info.FilePath = FilePath;
        Tiles.Add(Info);
        TileData.Add(MoveTemp(Data));
        UE_LOG(LogTemp, Log, TEXT("DEMParser: Loaded tile %s (%dx%d, lon %.2f~%.2f, lat %.2f~%.2f)"),
            *FilePath, Info.Width, Info.Height, Info.MinLon, Info.MaxLon, Info.MinLat, Info.MaxLat);
    }

    return bSuccess;
}

int32 UDEMParser::LoadTilesFromDirectory(const FString& DirectoryPath)
{
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *DirectoryPath, TEXT("*.*"), true, false);

    int32 LoadedCount = 0;
    for (const FString& File : Files)
    {
        const FString Ext = FPaths::GetExtension(File).ToLower();
        if (Ext == TEXT("tif") || Ext == TEXT("tiff") || Ext == TEXT("hgt") ||
            Ext == TEXT("png") || Ext == TEXT("r16") || Ext == TEXT("raw"))
        {
            if (LoadTile(File))
            {
                LoadedCount++;
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("DEMParser: Loaded %d tiles from %s"), LoadedCount, *DirectoryPath);
    return LoadedCount;
}

float UDEMParser::SampleElevation(double Lon, double Lat) const
{
    if (Tiles.Num() == 0)
    {
        return NoDataValue;
    }
    return SampleTile(0, Lon, Lat);
}

float UDEMParser::SampleElevationMultiTile(double Lon, double Lat) const
{
    const int32 TileIdx = FindTileForCoord(Lon, Lat);
    if (TileIdx == INDEX_NONE)
    {
        return NoDataValue;
    }
    return SampleTile(TileIdx, Lon, Lat);
}

float UDEMParser::SampleElevationBilinear(double Lon, double Lat) const
{
    const int32 TileIdx = FindTileForCoord(Lon, Lat);
    if (TileIdx == INDEX_NONE)
    {
        return NoDataValue;
    }
    return SampleTileBilinear(TileIdx, Lon, Lat);
}

bool UDEMParser::GetElevationGrid(
    double MinLon, double MinLat,
    double MaxLon, double MaxLat,
    float GridResolution,
    TArray<float>& OutGrid,
    int32& OutWidth, int32& OutHeight) const
{
    if (Tiles.Num() == 0)
    {
        return false;
    }

    // 估算每度经度对应的米数
    const double CenterLat = (MinLat + MaxLat) / 2.0;
    const double MetersPerDegreeLon = 111320.0 * FMath::Cos(FMath::DegreesToRadians(CenterLat));
    const double MetersPerDegreeLat = 110540.0;

    const double SpanLonM = (MaxLon - MinLon) * MetersPerDegreeLon;
    const double SpanLatM = (MaxLat - MinLat) * MetersPerDegreeLat;

    OutWidth = FMath::Max(1, FMath::CeilToInt(SpanLonM / GridResolution));
    OutHeight = FMath::Max(1, FMath::CeilToInt(SpanLatM / GridResolution));

    OutGrid.SetNumUninitialized(OutWidth * OutHeight);

    const double StepLon = (MaxLon - MinLon) / OutWidth;
    const double StepLat = (MaxLat - MinLat) / OutHeight;

    for (int32 Y = 0; Y < OutHeight; ++Y)
    {
        const double Lat = MinLat + (Y + 0.5) * StepLat;
        for (int32 X = 0; X < OutWidth; ++X)
        {
            const double Lon = MinLon + (X + 0.5) * StepLon;
            OutGrid[Y * OutWidth + X] = SampleElevationMultiTile(Lon, Lat);
        }
    }

    return true;
}

// ============ 格式解析 ============

bool UDEMParser::ParseGeoTIFF(const FString& FilePath, FDEMTileInfo& OutInfo, TArray<float>& OutData)
{
    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("DEMParser: Failed to read file %s"), *FilePath);
        return false;
    }

    // SRTM .hgt 格式：裸的 big-endian int16 数组，无头部
    // 标准尺寸：3601x3601 (1弧秒) 或 1201x1201 (3弧秒)
    const FString Ext = FPaths::GetExtension(FilePath).ToLower();
    if (Ext == TEXT("hgt"))
    {
        const int64 FileSize = FileBytes.Num();
        int32 Dim = 0;

        if (FileSize == 3601 * 3601 * 2)
        {
            Dim = 3601; // SRTM1 (1弧秒)
        }
        else if (FileSize == 1201 * 1201 * 2)
        {
            Dim = 1201; // SRTM3 (3弧秒)
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("DEMParser: Unrecognized .hgt file size: %lld"), FileSize);
            return false;
        }

        OutInfo.Width = Dim;
        OutInfo.Height = Dim;

        // 从文件名推断范围
        if (bAutoDetectSRTMBounds)
        {
            InferSRTMBounds(FPaths::GetBaseFilename(FilePath), OutInfo);
        }
        else
        {
            OutInfo.MinLon = ManualTileInfo.MinLon;
            OutInfo.MinLat = ManualTileInfo.MinLat;
            OutInfo.MaxLon = ManualTileInfo.MaxLon;
            OutInfo.MaxLat = ManualTileInfo.MaxLat;
        }

        // 解析 big-endian int16
        OutData.SetNumUninitialized(Dim * Dim);
        const uint8* Raw = FileBytes.GetData();
        for (int32 i = 0; i < Dim * Dim; ++i)
        {
            const int16 Value = (static_cast<int16>(Raw[i * 2]) << 8) | Raw[i * 2 + 1];
            // SRTM void = -32768
            OutData[i] = (Value == -32768) ? NoDataValue : static_cast<float>(Value);
        }

        return true;
    }

    // GeoTIFF：简化解析（读取最小头部信息 + 像素数据）
    // 完整 GeoTIFF 解析需要 TIFF tag 解析，这里做基础版本
    // TODO: 集成 libgeotiff 或 GDAL 获得完整支持

    if (FileBytes.Num() < 8)
    {
        return false;
    }

    // TIFF 魔数检查
    const bool bLittleEndian = (FileBytes[0] == 'I' && FileBytes[1] == 'I');
    const bool bBigEndian = (FileBytes[0] == 'M' && FileBytes[1] == 'M');
    if (!bLittleEndian && !bBigEndian)
    {
        UE_LOG(LogTemp, Error, TEXT("DEMParser: Not a valid TIFF file: %s"), *FilePath);
        return false;
    }

    // 简化实现：读取 TIFF IFD 获取宽高和数据偏移
    // 对于标准 SRTM GeoTIFF，数据通常是 int16 strip
    UE_LOG(LogTemp, Warning, TEXT("DEMParser: Full GeoTIFF parsing is simplified. "
        "For production use, integrate GDAL or libgeotiff. File: %s"), *FilePath);

    // 使用手动配置的 tile info
    OutInfo = ManualTileInfo;
    if (OutInfo.Width <= 0 || OutInfo.Height <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("DEMParser: ManualTileInfo required for GeoTIFF. "
            "Set Width/Height and geo bounds."));
        return false;
    }

    // 假设数据从文件尾部开始，int16 little-endian
    const int32 ExpectedPixels = OutInfo.Width * OutInfo.Height;
    const int32 DataBytes = ExpectedPixels * 2;
    if (FileBytes.Num() < DataBytes)
    {
        return false;
    }

    OutData.SetNumUninitialized(ExpectedPixels);
    const int32 Offset = FileBytes.Num() - DataBytes;
    const uint8* Raw = FileBytes.GetData() + Offset;

    for (int32 i = 0; i < ExpectedPixels; ++i)
    {
        int16 Value;
        if (bLittleEndian)
        {
            Value = static_cast<int16>(Raw[i * 2]) | (static_cast<int16>(Raw[i * 2 + 1]) << 8);
        }
        else
        {
            Value = (static_cast<int16>(Raw[i * 2]) << 8) | Raw[i * 2 + 1];
        }
        OutData[i] = (Value == -32768) ? NoDataValue : static_cast<float>(Value);
    }

    return true;
}

bool UDEMParser::ParseHeightmapPNG(const FString& FilePath, FDEMTileInfo& OutInfo, TArray<float>& OutData)
{
    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("DEMParser: Failed to read PNG %s"), *FilePath);
        return false;
    }

    // 使用 UE 的 ImageWrapper 解码 PNG
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

    if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(FileBytes.GetData(), FileBytes.Num()))
    {
        UE_LOG(LogTemp, Error, TEXT("DEMParser: Failed to decode PNG %s"), *FilePath);
        return false;
    }

    OutInfo.Width = ImageWrapper->GetWidth();
    OutInfo.Height = ImageWrapper->GetHeight();

    // 尝试从文件名推断 SRTM 范围，否则用手动配置
    if (bAutoDetectSRTMBounds && InferSRTMBounds(FPaths::GetBaseFilename(FilePath), OutInfo))
    {
        // OK
    }
    else
    {
        OutInfo.MinLon = ManualTileInfo.MinLon;
        OutInfo.MinLat = ManualTileInfo.MinLat;
        OutInfo.MaxLon = ManualTileInfo.MaxLon;
        OutInfo.MaxLat = ManualTileInfo.MaxLat;
    }

    TArray<uint8> RawPixels;
    if (!ImageWrapper->GetRaw(ERGBFormat::RGBA, 8, RawPixels))
    {
        UE_LOG(LogTemp, Error, TEXT("DEMParser: Failed to get raw pixels from %s"), *FilePath);
        return false;
    }

    const int32 NumPixels = OutInfo.Width * OutInfo.Height;
    OutData.SetNumUninitialized(NumPixels);

    for (int32 i = 0; i < NumPixels; ++i)
    {
        const uint8 R = RawPixels[i * 4 + 0];
        const uint8 G = RawPixels[i * 4 + 1];
        const uint8 B = RawPixels[i * 4 + 2];

        float Elevation = 0.0f;

        switch (PNGEncoding)
        {
            case EPNGHeightEncoding::Grayscale16:
            {
                // 灰度：用 R 通道（8bit → 假设 0~255 映射到 0~8848m）
                // 实际应使用 16bit 灰度，这里做 8bit 降级
                Elevation = static_cast<float>(R) / 255.0f * 8848.0f;
                break;
            }
            case EPNGHeightEncoding::MapboxTerrainRGB:
            {
                // Mapbox: elevation = -10000 + ((R * 256 * 256 + G * 256 + B) * 0.1)
                Elevation = -10000.0f + (R * 65536.0f + G * 256.0f + B) * 0.1f;
                break;
            }
            case EPNGHeightEncoding::CustomLinear:
            {
                Elevation = static_cast<float>(R) * CustomScale + CustomOffset;
                break;
            }
        }

        OutData[i] = Elevation;
    }

    return true;
}

bool UDEMParser::ParseHeightmapRAW(const FString& FilePath, FDEMTileInfo& OutInfo, TArray<float>& OutData)
{
    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *FilePath))
    {
        return false;
    }

    OutInfo = ManualTileInfo;

    // 自动推断尺寸：.r16 = uint16 per pixel
    if (OutInfo.Width <= 0 || OutInfo.Height <= 0)
    {
        // 尝试正方形推断
        const int32 NumPixels = FileBytes.Num() / 2;
        const int32 Dim = FMath::RoundToInt(FMath::Sqrt(static_cast<float>(NumPixels)));
        if (Dim * Dim == NumPixels)
        {
            OutInfo.Width = Dim;
            OutInfo.Height = Dim;
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("DEMParser: Cannot infer RAW dimensions. Set ManualTileInfo."));
            return false;
        }
    }

    const int32 NumPixels = OutInfo.Width * OutInfo.Height;
    OutData.SetNumUninitialized(NumPixels);

    const uint16* Raw16 = reinterpret_cast<const uint16*>(FileBytes.GetData());
    for (int32 i = 0; i < NumPixels && i * 2 + 1 < FileBytes.Num(); ++i)
    {
        // UE r16 格式：little-endian uint16，0-65535 映射到高程
        OutData[i] = static_cast<float>(Raw16[i]);
    }

    return true;
}

bool UDEMParser::InferSRTMBounds(const FString& FileName, FDEMTileInfo& OutInfo) const
{
    // SRTM 命名格式：N39E116 或 S12W077
    if (FileName.Len() < 7)
    {
        return false;
    }

    const TCHAR NS = FileName[0];
    const TCHAR EW = FileName.Len() > 3 ? FileName[3] : '\0';

    if ((NS != 'N' && NS != 'S') || (EW != 'E' && EW != 'W'))
    {
        return false;
    }

    const int32 Lat = FCString::Atoi(*FileName.Mid(1, 2));
    const int32 Lon = FCString::Atoi(*FileName.Mid(4, 3));

    OutInfo.MinLat = (NS == 'N') ? Lat : -Lat;
    OutInfo.MaxLat = OutInfo.MinLat + 1.0;
    OutInfo.MinLon = (EW == 'E') ? Lon : -Lon;
    OutInfo.MaxLon = OutInfo.MinLon + 1.0;

    return true;
}

EDEMFormat UDEMParser::DetectFormat(const FString& FilePath) const
{
    const FString Ext = FPaths::GetExtension(FilePath).ToLower();

    if (Ext == TEXT("tif") || Ext == TEXT("tiff") || Ext == TEXT("hgt"))
    {
        return EDEMFormat::GeoTIFF;
    }
    if (Ext == TEXT("png"))
    {
        return EDEMFormat::HeightmapPNG;
    }
    if (Ext == TEXT("r16") || Ext == TEXT("raw"))
    {
        return EDEMFormat::HeightmapRAW;
    }

    return EDEMFormat::GeoTIFF; // 默认
}

int32 UDEMParser::FindTileForCoord(double Lon, double Lat) const
{
    for (int32 i = 0; i < Tiles.Num(); ++i)
    {
        const FDEMTileInfo& T = Tiles[i];
        if (Lon >= T.MinLon && Lon <= T.MaxLon && Lat >= T.MinLat && Lat <= T.MaxLat)
        {
            return i;
        }
    }
    return INDEX_NONE;
}

float UDEMParser::SampleTile(int32 TileIndex, double Lon, double Lat) const
{
    if (!Tiles.IsValidIndex(TileIndex) || !TileData.IsValidIndex(TileIndex))
    {
        return NoDataValue;
    }

    const FDEMTileInfo& Info = Tiles[TileIndex];
    const TArray<float>& Data = TileData[TileIndex];

    // 经纬度 → 像素坐标
    const double NormX = (Lon - Info.MinLon) / (Info.MaxLon - Info.MinLon);
    const double NormY = 1.0 - (Lat - Info.MinLat) / (Info.MaxLat - Info.MinLat); // 翻转Y（栅格从上到下）

    const int32 PX = FMath::Clamp(FMath::FloorToInt(NormX * Info.Width), 0, Info.Width - 1);
    const int32 PY = FMath::Clamp(FMath::FloorToInt(NormY * Info.Height), 0, Info.Height - 1);

    const int32 Idx = PY * Info.Width + PX;
    return Data.IsValidIndex(Idx) ? Data[Idx] : NoDataValue;
}

float UDEMParser::SampleTileBilinear(int32 TileIndex, double Lon, double Lat) const
{
    if (!Tiles.IsValidIndex(TileIndex) || !TileData.IsValidIndex(TileIndex))
    {
        return NoDataValue;
    }

    const FDEMTileInfo& Info = Tiles[TileIndex];
    const TArray<float>& Data = TileData[TileIndex];

    const double FX = (Lon - Info.MinLon) / (Info.MaxLon - Info.MinLon) * Info.Width - 0.5;
    const double FY = (1.0 - (Lat - Info.MinLat) / (Info.MaxLat - Info.MinLat)) * Info.Height - 0.5;

    const int32 X0 = FMath::Clamp(FMath::FloorToInt(FX), 0, Info.Width - 2);
    const int32 Y0 = FMath::Clamp(FMath::FloorToInt(FY), 0, Info.Height - 2);
    const float SX = static_cast<float>(FX - X0);
    const float SY = static_cast<float>(FY - Y0);

    auto SafeGet = [&](int32 X, int32 Y) -> float
    {
        const int32 Idx = Y * Info.Width + X;
        return Data.IsValidIndex(Idx) ? Data[Idx] : NoDataValue;
    };

    const float V00 = SafeGet(X0, Y0);
    const float V10 = SafeGet(X0 + 1, Y0);
    const float V01 = SafeGet(X0, Y0 + 1);
    const float V11 = SafeGet(X0 + 1, Y0 + 1);

    // 任一角是 NODATA 就退回最近邻
    if (V00 == NoDataValue || V10 == NoDataValue || V01 == NoDataValue || V11 == NoDataValue)
    {
        return SampleTile(TileIndex, Lon, Lat);
    }

    return FMath::Lerp(
        FMath::Lerp(V00, V10, SX),
        FMath::Lerp(V01, V11, SX),
        SY
    );
}
