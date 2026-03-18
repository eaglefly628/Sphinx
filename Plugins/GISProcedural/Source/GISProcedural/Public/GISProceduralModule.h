// GISProceduralModule.h - 插件模块头文件
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FGISProceduralModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
