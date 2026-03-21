// GISProceduralModule.h - 插件模块头文件
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// 统一日志类别，便于按模块过滤调试输出
// 用法: UE_LOG(LogGIS, Log, TEXT("..."));
// 在编辑器 Output Log 中可用 LogGIS 过滤
DECLARE_LOG_CATEGORY_EXTERN(LogGIS, Log, All);

class FGISProceduralModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
