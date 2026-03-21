// GISProceduralModule.cpp - 插件模块实现
#include "GISProceduralModule.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogGIS);

#define LOCTEXT_NAMESPACE "FGISProceduralModule"

void FGISProceduralModule::StartupModule()
{
    UE_LOG(LogGIS, Log, TEXT("GISProcedural: Module started"));
}

void FGISProceduralModule::ShutdownModule()
{
    UE_LOG(LogGIS, Log, TEXT("GISProcedural: Module shutdown"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGISProceduralModule, GISProcedural)
