// EagleWalk AngelScript environment test
// Drop this actor into any level to verify AngelScript is working

class AEagleWalkTest : AActor
{
    default:
    bReplicates = false;

    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent Root;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Print("========================================");
        Print("  EagleWalk AngelScript OK!");
        Print("  Engine: UE 5.6 + AngelScript");
        Print("========================================");
    }
}
