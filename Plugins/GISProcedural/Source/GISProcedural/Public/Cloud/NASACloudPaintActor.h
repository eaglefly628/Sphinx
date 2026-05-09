// NASACloudPaintActor.h - NASA 云数据 → UDS Painted Cloud 桥接 Actor
// 通过 UDS_CloudPaintActor_Interface（BP 子类实现）让 UDS 主动每帧拉取 NASA 数据
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NASACloudPaintActor.generated.h"

class UTexture;
class UCanvas;

/**
 * NASA 云覆盖度 → UDS Painted Cloud 桥接 Actor
 *
 * 工作机制（基于 UDS 9.3A 的 painted cloud 管线反向工程）：
 * 1. UDS_Ultra_Dynamic_Sky.UpdatePaintedCloudCoverageTarget() 每次 RT 刷新时：
 *    a. SnappedToGrid(camera) 计算 Cloud Coverage Target Location（世界 XY）
 *    b. self.Cloud Coverage Target Mapping() → Vector(centerX, centerY, fullSize) 单位 cm
 *    c. BeginDrawCanvasToRenderTarget(Cloud Coverage Render Target)
 *    d. 遍历 Cloud Paint Actors Manager.FilteredAndSortedActors
 *    e. 对每个 actor 调用 UDS_CloudPaintActor_Interface.DrawToCloudPaintTarget(...)
 *    f. EndDrawCanvasToRenderTarget
 * 2. 我们的 actor 通过 BP 子类实现接口，把 NASA 贴图绘制到 Canvas 上
 * 3. UDS volumetric cloud shader 通过 Cloud Coverage Render Target 采样得到 NASA 云分布
 *
 * 用法：
 * 1. 编辑器创建 BP 子类（继承自 ANASACloudPaintActor）
 * 2. BP Class Settings → Implemented Interfaces → 添加 UDS_CloudPaintActor_Interface
 * 3. My Blueprint → Interfaces → Override "Draw to Cloud Paint Target"
 *    函数体内只放一个节点：调用 self.DrawNASAToCanvas(...)，连上所有同名参数
 * 4. 把 BP 子类 spawn 到关卡，Set NASACoverageTexture = NASA 渲染产生的 RT
 * 5. UDS 自动通过 Cloud Paint Actors Manager 发现并每帧调用
 */
UCLASS(Blueprintable, ClassGroup = "GIS", meta = (DisplayName = "NASA Cloud Paint Actor"))
class GISPROCEDURAL_API ANASACloudPaintActor : public AActor
{
	GENERATED_BODY()

public:
	ANASACloudPaintActor();

	// ============ NASA 数据源 ============

	/** NASA 云覆盖度贴图。R 通道 = 云密度 0-1（0 无云、1 厚云）。可以是 RenderTarget2D 或 Texture2D */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NASA Cloud")
	TObjectPtr<UTexture> NASACoverageTexture = nullptr;

	/** NASA 贴图覆盖的世界中心 XY（cm，UE 世界坐标）。仅在 bUseWorldMapping=true 时使用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NASA Cloud")
	FVector2D NASAWorldCenter = FVector2D::ZeroVector;

	/** NASA 贴图覆盖的世界尺寸（cm，正方形边长）。默认 200km。仅在 bUseWorldMapping=true 时使用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NASA Cloud", meta = (ClampMin = "0"))
	float NASAWorldSize = 20000000.0f;

	/** 是否按 UDS 的 TargetMapping 做世界对齐绘制。
	 *  false（默认）= NASA 贴图直接铺满 Canvas，不考虑相机位置。最简单最快。
	 *  true = 按世界坐标对齐，相机移动时贴图会"贴"在世界上。需要 NASA 覆盖区设置正确。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NASA Cloud")
	bool bUseWorldMapping = false;

	/** 绘制颜色乘数。默认白色 = 直接使用纹理颜色。可降低 alpha 让 UDS 默认 coverage 透出来 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NASA Cloud")
	FLinearColor RenderColor = FLinearColor::White;

	/** 启用绘制。false 时 actor 仍被 manager 发现但不画任何东西（DrawNASAToCanvas 返回 false） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NASA Cloud")
	bool bEnabled = true;

	// ============ Manager 排序 / 过滤元数据 ============
	// UDS 的 Cloud Paint Actors Manager (UDS_InterfaceActorArrayManager) 会用 Filter And Sort Array
	// 决定 actor 顺序。这里暴露一个 priority 字段供子类或场景实例覆盖。
	// 数值越大越后绘（覆盖前面）。

	/** 绘制优先级。数值越大越后画（覆盖之前的）。默认 100（painter cell 通常 = 0） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NASA Cloud")
	int32 PaintPriority = 100;

	// ============ 给 BP 子类调用的核心函数 ============

	/**
	 * 把 NASACoverageTexture 绘制到 UDS 的 Canvas 上。
	 *
	 * BP 子类 override "Draw to Cloud Paint Target" 接口函数时，函数体调用本函数即可。
	 * 所有同名参数（Canvas / TargetMapping / TargetRes / CanAdd / CanSubtract / CloudPaintingActive）
	 * 直接从 BP 接口函数的入参连过来。
	 *
	 * @param Canvas              UDS 提供的 Canvas（绑定到 Cloud Coverage Render Target）
	 * @param TargetMapping       UDS 的 Cloud Coverage Target Mapping(): (centerX_world, centerY_world, fullSize_world) 单位 cm
	 * @param TargetRes           Canvas 像素分辨率（正方形边长，从 UDS Cloud Coverage Target Resolution 来）
	 * @param bCanAddCoverage     UDS 当前是否允许"加"云
	 * @param bCanSubtractCoverage UDS 当前是否允许"减"云
	 * @param bCloudPaintingActive UDS 全局 painted cloud 开关
	 * @return true = 成功绘制，false = 跳过（未启用 / 缺贴图 / 参数无效）
	 */
	UFUNCTION(BlueprintCallable, Category = "NASA Cloud")
	bool DrawNASAToCanvas(
		UCanvas* Canvas,
		FVector TargetMapping,
		int32 TargetRes,
		bool bCanAddCoverage,
		bool bCanSubtractCoverage,
		bool bCloudPaintingActive);

	/**
	 * 静态版本：从任何 BP 调用，不需要 actor 实例。
	 *
	 * 用途：当 BP 子类继承自 UDS_Cloud_Paint_Container（绕开 Add Interface picker bug 的方案 B）时，
	 * 在 override 的 "Draw to Cloud Paint Target" 函数体内调用本函数。
	 * 所有原本是 UPROPERTY 的属性这里作为 input 参数传入（在 BP 子类用 BP variables 持有）。
	 *
	 * 与实例版本 DrawNASAToCanvas 行为完全一致，只是参数化了内部状态。
	 */
	UFUNCTION(BlueprintCallable, Category = "NASA Cloud",
		meta = (DisplayName = "Draw NASA Texture To Canvas (Static)"))
	static bool DrawNASATextureToCanvas(
		UCanvas* Canvas,
		UTexture* NASATexture,
		FVector TargetMapping,
		int32 TargetRes,
		FVector2D InNASAWorldCenter,
		float InNASAWorldSize,
		bool bInUseWorldMapping,
		FLinearColor InRenderColor,
		bool bCloudPaintingActive);

protected:
	virtual void BeginPlay() override;
};
