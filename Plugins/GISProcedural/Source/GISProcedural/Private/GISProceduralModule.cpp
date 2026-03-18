// GISProceduralModule.cpp - 插件模块实现
#include "GISProceduralModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FGISProceduralModule"

void FGISProceduralModule::StartupModule()
{
    UE_LOG(LogTemp, Log, TEXT("GISProcedural: Module started"));
}

void FGISProceduralModule::ShutdownModule()
{
    UE_LOG(LogTemp, Log, TEXT("GISProcedural: Module shutdown"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGISProceduralModule, GISProcedural)
