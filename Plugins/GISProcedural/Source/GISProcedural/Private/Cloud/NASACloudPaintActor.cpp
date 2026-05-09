// NASACloudPaintActor.cpp - NASA 云数据 → UDS Painted Cloud 桥接 Actor 实现
#include "Cloud/NASACloudPaintActor.h"

#include "Engine/Canvas.h"
#include "Engine/Texture.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogGIS_NASACloud, Log, All);

ANASACloudPaintActor::ANASACloudPaintActor()
{
	PrimaryActorTick.bCanEverTick = false;  // UDS 主动调用，无需 tick
	bReplicates = false;
	SetActorEnableCollision(false);
}

void ANASACloudPaintActor::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogGIS_NASACloud, Log, TEXT("===== NASACloudPaintActor BeginPlay START ====="));
	UE_LOG(LogGIS_NASACloud, Log,
		TEXT("  Actor=%s  Texture=%s  WorldCenter=(%.1f, %.1f)  WorldSize=%.1f cm  UseMapping=%s  Enabled=%s  Priority=%d"),
		*GetName(),
		NASACoverageTexture ? *NASACoverageTexture->GetName() : TEXT("(null)"),
		NASAWorldCenter.X, NASAWorldCenter.Y,
		NASAWorldSize,
		bUseWorldMapping ? TEXT("true") : TEXT("false"),
		bEnabled ? TEXT("true") : TEXT("false"),
		PaintPriority);

	if (!NASACoverageTexture)
	{
		UE_LOG(LogGIS_NASACloud, Warning,
			TEXT("  NASACoverageTexture 未设置 — DrawNASAToCanvas 将全部跳过。请在 Spawn 后赋值 NASA RT。"));
	}

	UE_LOG(LogGIS_NASACloud, Log, TEXT("===== NASACloudPaintActor BeginPlay END ====="));
}

bool ANASACloudPaintActor::DrawNASAToCanvas(
	UCanvas* Canvas,
	FVector TargetMapping,
	int32 TargetRes,
	bool bCanAddCoverage,
	bool bCanSubtractCoverage,
	bool bCloudPaintingActive)
{
	if (!bEnabled || !bCloudPaintingActive)
	{
		return false;
	}
	if (!Canvas || !NASACoverageTexture || TargetRes <= 0)
	{
		return false;
	}

	// 计算 ScreenPosition / ScreenSize / CoordinatePosition / CoordinateSize
	FVector2D ScreenPosition;
	FVector2D ScreenSize;
	FVector2D CoordinatePosition(0.0f, 0.0f);
	FVector2D CoordinateSize(1.0f, 1.0f);

	if (bUseWorldMapping && NASAWorldSize > KINDA_SMALL_NUMBER)
	{
		// === 世界对齐绘制 ===
		// 解码 TargetMapping = (CenterX, CenterY, FullSize) 单位 cm
		// （由 UDS UpdatePaintedCloudCoverageTarget 内 Cloud Coverage Target Mapping() 函数生成；
		//  cell BP 反解证实：TargetLocation = (Mapping.X + Z/2, Mapping.Y + Z/2) = TopRight 世界角）
		const float MappingSize = static_cast<float>(TargetMapping.Z);
		if (MappingSize <= KINDA_SMALL_NUMBER)
		{
			// Z=0 退化：fallback 到 full canvas
			ScreenPosition = FVector2D::ZeroVector;
			ScreenSize = FVector2D(static_cast<float>(TargetRes), static_cast<float>(TargetRes));
		}
		else
		{
			// Canvas 覆盖的世界范围: [(MapCenter.X - Size/2, MapCenter.Y - Size/2)] 到 [(+Size/2, +Size/2)]
			// NASA 覆盖的世界范围:   [(NASACenter.X - NASASize/2, ...)] 到 [(+NASASize/2, ...)]
			// Canvas pixel-per-world = TargetRes / MappingSize
			const float PixelsPerWorld = static_cast<float>(TargetRes) / MappingSize;
			const FVector2D MappingCenter(static_cast<float>(TargetMapping.X), static_cast<float>(TargetMapping.Y));
			const FVector2D MappingTopLeft = MappingCenter - FVector2D(MappingSize, MappingSize) * 0.5f;

			const FVector2D NASATopLeft = NASAWorldCenter - FVector2D(NASAWorldSize, NASAWorldSize) * 0.5f;
			ScreenPosition = (NASATopLeft - MappingTopLeft) * PixelsPerWorld;
			ScreenSize = FVector2D(NASAWorldSize, NASAWorldSize) * PixelsPerWorld;
		}
	}
	else
	{
		// === 全画布铺满（默认、最快） ===
		ScreenPosition = FVector2D::ZeroVector;
		ScreenSize = FVector2D(static_cast<float>(TargetRes), static_cast<float>(TargetRes));
	}

	// UCanvas::K2_DrawTexture 等价于 BP "Draw Texture" 节点
	// 选择 BLEND_Opaque 与 cell BP 行为对齐（直接覆盖 RT）
	Canvas->K2_DrawTexture(
		NASACoverageTexture,
		ScreenPosition,
		ScreenSize,
		CoordinatePosition,
		CoordinateSize,
		RenderColor,
		EBlendMode::BLEND_Opaque,
		0.0f,                       // Rotation
		FVector2D(0.5f, 0.5f)       // PivotPoint
	);

	return true;
}
