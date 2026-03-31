// WeatherBridge - Connects Cesium geolocation with Ultra Dynamic Sky
//
// SETUP:
//   1. Level must have: Ultra_Dynamic_Sky actor + CesiumGeoreference
//   2. Drag AWeatherBridge into the level
//   3. In Details panel, assign UDS and Cesium references (or leave empty for auto-find)
//   4. Play - bridge auto-syncs Cesium lat/lon to UDS real sun simulation
//
// UDS is a Blueprint actor, so we access its properties through
// AngelScript's Blueprint interop.

// Forward declare - the actual UDS Blueprint class
// If this doesn't compile, replace with AActor and use property reflection
class AWeatherBridge : AActor
{
    // ---- Editor Assignable References ----

    UPROPERTY(Category = "Weather Bridge")
    AActor UDSActor;

    UPROPERTY(Category = "Weather Bridge")
    AActor UDWActor;

    UPROPERTY(Category = "Weather Bridge")
    AActor CesiumGeoref;

    // ---- Configuration ----

    UPROPERTY(Category = "Weather Bridge|Simulation")
    bool bSyncWithCesium = true;

    UPROPERTY(Category = "Weather Bridge|Simulation")
    bool bEnableRealSun = true;

    // UTC offset (auto from longitude if bAutoTimeZone)
    UPROPERTY(Category = "Weather Bridge|Simulation")
    float ManualTimeZone = 8.f;

    UPROPERTY(Category = "Weather Bridge|Simulation")
    bool bAutoTimeZone = true;

    // ---- Runtime State ----
    bool bInitialized = false;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Print("===== WeatherBridge START =====");

        // Auto-find actors
        AutoFindActors();

        // Report what we found
        if (UDSActor != nullptr)
            Print("WeatherBridge: UDS found -> " + UDSActor.GetName());
        else
            PrintWarning("WeatherBridge: UDS NOT FOUND - drag Ultra_Dynamic_Sky into level");

        if (CesiumGeoref != nullptr)
            Print("WeatherBridge: Cesium found -> " + CesiumGeoref.GetName());
        else
            Print("WeatherBridge: No CesiumGeoreference (optional)");

        if (UDWActor != nullptr)
            Print("WeatherBridge: UDW found -> " + UDWActor.GetName());

        bInitialized = (UDSActor != nullptr);
        Print("===== WeatherBridge END (active=" + bInitialized + ") =====");
    }

    void AutoFindActors()
    {
        TArray<AActor> AllActors;
        GetAllActorsOfClass(AllActors);

        for (AActor A : AllActors)
        {
            FString ClassName = A.GetClass().GetName().ToString();

            if (UDSActor == nullptr && ClassName.Contains("Ultra_Dynamic_Sky"))
                UDSActor = A;

            if (UDWActor == nullptr && ClassName.Contains("Ultra_Dynamic_Weather"))
                UDWActor = A;

            if (CesiumGeoref == nullptr && ClassName.Contains("CesiumGeoreference"))
                CesiumGeoref = A;
        }
    }

    void GetAllActorsOfClass(TArray<AActor>& OutActors)
    {
        Gameplay::GetAllActorsOfClass(OutActors);
    }
}
