// RasterLandCoverParser.h - ESA WorldCover JSON 栅格解析
#pragma once

#include "CoreMinimal.h"
#include "Data/LandCoverGrid.h"
#include "RasterLandCoverParser.generated.h"

/**
 * ESA WorldCover 栅格数据解析器
 *
 * 读取预处理管线输出的 landcover.json 文件：
 * {
 *     "width": 100,
 *     "height": 100,
 *     "cell_size_m": 10.0,
 *     "classes": [10, 10, 50, 30, ...]
 * }
 *
 * 解析结果存入 FLandCoverGrid 供 LandUseClassifier::FuseLandCoverData 使用
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API URasterLandCoverParser : public UObject
{
    GENERATED_BODY()

public:
    /**
     * 从 JSON 文件解析 LandCover 栅格
     * @param FilePath  landcover.json 文件的绝对路径
     * @param OutGrid   输出的栅格数据
     * @return 是否解析成功
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Data")
    static bool ParseFile(const FString& FilePath, FLandCoverGrid& OutGrid);

    /**
     * 从 JSON 字符串解析 LandCover 栅格
     * @param JsonString  JSON 内容
     * @param OutGrid     输出的栅格数据
     * @return 是否解析成功
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Data")
    static bool ParseString(const FString& JsonString, FLandCoverGrid& OutGrid);

    /**
     * 对栅格区域进行多数类统计
     * @param Grid      完整栅格
     * @param StartCol  起始列
     * @param StartRow  起始行
     * @param RegionW   区域宽度
     * @param RegionH   区域高度
     * @return 多数类的 ESA WorldCover 类码
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Data")
    static uint8 MajorityClassInRegion(
        const FLandCoverGrid& Grid,
        int32 StartCol, int32 StartRow,
        int32 RegionW, int32 RegionH);
};
