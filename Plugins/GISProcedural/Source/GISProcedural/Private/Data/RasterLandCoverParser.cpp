// RasterLandCoverParser.cpp - ESA WorldCover JSON 栅格解析实现
#include "Data/RasterLandCoverParser.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool URasterLandCoverParser::ParseFile(const FString& FilePath, FLandCoverGrid& OutGrid)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("RasterLandCoverParser: Failed to load: %s"), *FilePath);
        return false;
    }

    return ParseString(JsonString, OutGrid);
}

bool URasterLandCoverParser::ParseString(const FString& JsonString, FLandCoverGrid& OutGrid)
{
    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("RasterLandCoverParser: Failed to parse JSON"));
        return false;
    }

    OutGrid.Width = RootObject->GetIntegerField(TEXT("width"));
    OutGrid.Height = RootObject->GetIntegerField(TEXT("height"));
    OutGrid.ResolutionM = RootObject->GetNumberField(TEXT("cell_size_m"));

    const TArray<TSharedPtr<FJsonValue>>* ClassesArray = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("classes"), ClassesArray))
    {
        UE_LOG(LogTemp, Error, TEXT("RasterLandCoverParser: No 'classes' array in JSON"));
        return false;
    }

    const int32 ExpectedSize = OutGrid.Width * OutGrid.Height;
    if (ClassesArray->Num() != ExpectedSize)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("RasterLandCoverParser: classes array size %d != expected %d (width*height)"),
            ClassesArray->Num(), ExpectedSize);
    }

    OutGrid.ClassGrid.Reserve(ClassesArray->Num());
    for (const TSharedPtr<FJsonValue>& Val : *ClassesArray)
    {
        OutGrid.ClassGrid.Add(static_cast<uint8>(Val->AsNumber()));
    }

    UE_LOG(LogTemp, Log, TEXT("RasterLandCoverParser: Parsed %dx%d grid (%.1fm resolution, %d cells)"),
        OutGrid.Width, OutGrid.Height, OutGrid.ResolutionM, OutGrid.ClassGrid.Num());

    return OutGrid.IsValid();
}

uint8 URasterLandCoverParser::MajorityClassInRegion(
    const FLandCoverGrid& Grid,
    int32 StartCol, int32 StartRow,
    int32 RegionW, int32 RegionH)
{
    if (!Grid.IsValid())
    {
        return 0;
    }

    // 统计各类码出现次数
    TMap<uint8, int32> ClassCounts;
    int32 TotalSampled = 0;

    const int32 EndCol = FMath::Min(StartCol + RegionW, Grid.Width);
    const int32 EndRow = FMath::Min(StartRow + RegionH, Grid.Height);

    for (int32 Row = FMath::Max(StartRow, 0); Row < EndRow; ++Row)
    {
        for (int32 Col = FMath::Max(StartCol, 0); Col < EndCol; ++Col)
        {
            const int32 Idx = Row * Grid.Width + Col;
            if (Idx >= 0 && Idx < Grid.ClassGrid.Num())
            {
                const uint8 Code = Grid.ClassGrid[Idx];
                if (Code > 0)  // 排除 NoData (0)
                {
                    ClassCounts.FindOrAdd(Code) += 1;
                    TotalSampled++;
                }
            }
        }
    }

    if (TotalSampled == 0)
    {
        return 0;
    }

    // 找票数最多的类码
    uint8 WinnerCode = 0;
    int32 MaxCount = 0;
    for (const auto& Pair : ClassCounts)
    {
        if (Pair.Value > MaxCount)
        {
            MaxCount = Pair.Value;
            WinnerCode = Pair.Key;
        }
    }

    return WinnerCode;
}
