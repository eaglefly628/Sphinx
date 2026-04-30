// EagleCloudModule.cpp
#include "EagleCloudModule.h"

DEFINE_LOG_CATEGORY(LogEagleCloud);

#define LOCTEXT_NAMESPACE "FEagleCloudModule"

void FEagleCloudModule::StartupModule()
{
    UE_LOG(LogEagleCloud, Log, TEXT("EagleCloud: Module started"));
}

void FEagleCloudModule::ShutdownModule()
{
    UE_LOG(LogEagleCloud, Log, TEXT("EagleCloud: Module shutdown"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FEagleCloudModule, EagleCloud)
