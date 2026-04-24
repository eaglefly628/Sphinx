#include "SphinxFlightModule.h"

DEFINE_LOG_CATEGORY(LogSphinxFlight);

#define LOCTEXT_NAMESPACE "FSphinxFlightModule"

void FSphinxFlightModule::StartupModule()
{
    UE_LOG(LogSphinxFlight, Log, TEXT("SphinxFlight module started"));
}

void FSphinxFlightModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSphinxFlightModule, SphinxFlight)
