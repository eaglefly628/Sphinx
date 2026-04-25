#include "EagleWalkMassAI.h"

DEFINE_LOG_CATEGORY(LogEagleMassAI);

void FEagleWalkMassAIModule::StartupModule()
{
	UE_LOG(LogEagleMassAI, Log, TEXT("EagleWalkMassAI module started"));
}

void FEagleWalkMassAIModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FEagleWalkMassAIModule, EagleWalkMassAI);
