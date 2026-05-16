// NASACloudPaintActor.cpp - NASA 云数据 → UDS Painted Cloud 桥接 Actor 实现
#include "Cloud/NASACloudPaintActor.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
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
	if (!bEnabled)
	{
		return false;
	}
	// 转调 static 实现，把实例 UPROPERTY 当参数传进去
	return DrawNASATextureToCanvas(
		Canvas,
		NASACoverageTexture,
		TargetMapping,
		TargetRes,
		NASAWorldCenter,
		NASAWorldSize,
		bUseWorldMapping,
		RenderColor,
		bCloudPaintingActive);
}

bool ANASACloudPaintActor::DrawNASATextureToCanvas(
	UCanvas* Canvas,
	UTexture* NASATexture,
	FVector TargetMapping,
	int32 TargetRes,
	FVector2D InNASAWorldCenter,
	float InNASAWorldSize,
	bool bInUseWorldMapping,
	FLinearColor InRenderColor,
	bool bCloudPaintingActive)
{
	// === 入口 log：UDS 调到了就刷 ===
	UE_LOG(LogGIS_NASACloud, Log,
		TEXT("DrawNASATextureToCanvas: bActive=%d Canvas=%s NASATex=%s TargetRes=%d Map=(%.0f,%.0f,%.0f)"),
		bCloudPaintingActive ? 1 : 0,
		Canvas ? TEXT("ok") : TEXT("null"),
		NASATexture ? *NASATexture->GetName() : TEXT("null"),
		TargetRes,
		TargetMapping.X, TargetMapping.Y, TargetMapping.Z);

	if (GEngine)
	{
		const FString EntryMsg = FString::Printf(
			TEXT("[NASA-IN] Active=%d Canvas=%s Tex=%s Res=%d"),
			bCloudPaintingActive ? 1 : 0,
			Canvas ? TEXT("ok") : TEXT("null"),
			NASATexture ? *NASATexture->GetName() : TEXT("null"),
			TargetRes);
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(103), 1.5f, FColor::Orange, EntryMsg);
	}

	if (!bCloudPaintingActive)
	{
		UE_LOG(LogGIS_NASACloud, Warning, TEXT("  EARLY EXIT: bCloudPaintingActive=false"));
		if (GEngine) GEngine->AddOnScreenDebugMessage(static_cast<uint64>(104), 1.5f, FColor::Red, TEXT("[NASA-OUT] PaintingActive=false"));
		return false;
	}
	if (!Canvas || !NASATexture || TargetRes <= 0)
	{
		UE_LOG(LogGIS_NASACloud, Warning, TEXT("  EARLY EXIT: Canvas/NASATex/TargetRes invalid"));
		if (GEngine) GEngine->AddOnScreenDebugMessage(static_cast<uint64>(104), 1.5f, FColor::Red, TEXT("[NASA-OUT] Canvas/Tex/Res invalid"));
		return false;
	}

	// 计算 ScreenPosition / ScreenSize / CoordinatePosition / CoordinateSize
	FVector2D ScreenPosition;
	FVector2D ScreenSize;
	FVector2D CoordinatePosition(0.0f, 0.0f);
	FVector2D CoordinateSize(1.0f, 1.0f);

	if (bInUseWorldMapping && InNASAWorldSize > KINDA_SMALL_NUMBER)
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

			const FVector2D NASATopLeft = InNASAWorldCenter - FVector2D(InNASAWorldSize, InNASAWorldSize) * 0.5f;
			ScreenPosition = (NASATopLeft - MappingTopLeft) * PixelsPerWorld;
			ScreenSize = FVector2D(InNASAWorldSize, InNASAWorldSize) * PixelsPerWorld;
		}
	}
	else
	{
		// === 全画布铺满（默认、最快） ===
		ScreenPosition = FVector2D::ZeroVector;
		ScreenSize = FVector2D(static_cast<float>(TargetRes), static_cast<float>(TargetRes));
	}

	// UCanvas::K2_DrawTexture 等价于 BP "Draw Texture" 节点
	// BlendMode = Additive：避免 R/G/B 三个通道互相擦除
	//   UDS painted cloud RT 通道编码（实测）：
	//     R = Zero Coverage   (强制无云 / 减云)
	//     G = Mid Coverage    (部分云)
	//     B = Full Coverage   (强制满云 / 加云)
	//   配合 RenderColor=(0,0,1,1) 走 B 通道：NASA 亮处加云，NASA 暗处不动
	//   配合 RenderColor=(1,0,0,1) 走 R 通道：NASA 亮处清空，NASA 暗处不动
	//   Additive 让多个 actor 的 Draw 累加，painter cell + NASA actor 共存不互相擦
	Canvas->K2_DrawTexture(
		NASATexture,
		ScreenPosition,
		ScreenSize,
		CoordinatePosition,
		CoordinateSize,
		InRenderColor,
		EBlendMode::BLEND_Additive,
		0.0f,                       // Rotation
		FVector2D(0.5f, 0.5f)       // PivotPoint
	);

	// On-screen debug — 看 UDS 给我们的 mapping 参数 + 我们计算出的 canvas 坐标
	if (GEngine)
	{
		const FString Msg = FString::Printf(
			TEXT("[NASA] Map=(%.0f,%.0f,%.0f) Res=%d UseWorldMap=%s  ScreenPos=(%.1f,%.1f) ScreenSize=(%.1f,%.1f)  Color=(%.2f,%.2f,%.2f,%.2f)"),
			TargetMapping.X, TargetMapping.Y, TargetMapping.Z,
			TargetRes,
			bInUseWorldMapping ? TEXT("true") : TEXT("false"),
			ScreenPosition.X, ScreenPosition.Y,
			ScreenSize.X, ScreenSize.Y,
			InRenderColor.R, InRenderColor.G, InRenderColor.B, InRenderColor.A);
		GEngine->AddOnScreenDebugMessage(
			/*Key=*/ static_cast<uint64>(102),
			/*Time=*/ 1.5f,
			FColor::Yellow,
			Msg);
	}

	return true;
}
