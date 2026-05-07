// EagleCloudUDSBridge.cpp
//
// All entry points are BlueprintImplementableEvent — implementations live in
// the BP subclass (BP_EagleCloudBridge). C++ only provides the construction
// and reflection metadata.
//
#include "EagleCloudUDSBridge.h"

AEagleCloudUDSBridge::AEagleCloudUDSBridge()
{
    PrimaryActorTick.bCanEverTick = false;
}
