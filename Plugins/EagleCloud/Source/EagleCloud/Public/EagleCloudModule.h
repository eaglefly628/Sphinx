// EagleCloudModule.h
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Unified log category for EagleCloud
// Filter in Output Log: LogEagleCloud
DECLARE_LOG_CATEGORY_EXTERN(LogEagleCloud, Log, All);

class FEagleCloudModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
