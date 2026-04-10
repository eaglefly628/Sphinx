// WeatherDebugMenu - In-game debug panel for UDS/UDW control
//
// USAGE:
//   Drag into level, Play, press F1 to toggle menu
//   Use keyboard shortcuts to control weather/time
//
// CONTROLS:
//   F1        - Toggle debug menu
//   1-9       - Switch weather presets
//   +/-       - Adjust time speed
//   Left/Right - Adjust time of day
//   Up/Down   - Adjust cloud coverage
//   F/G       - Adjust fog
//   R         - Toggle rain
//   T         - Toggle thunder
//   N         - Jump to night
//   D         - Jump to day

class AWeatherDebugMenu : AActor
{
    // ---- References (auto-found) ----
    AActor UDSActor;
    AActor UDWActor;

    // ---- State ----
    bool bMenuVisible = false;
    bool bKeyF1WasDown = false;

    // Tracked values (read from UDS/UDW each frame)
    float TimeOfDay = 1200.f;
    float TimeSpeed = 1.f;
    float CloudCoverage = 0.5f;
    float FogAmount = 0.f;
    float RainAmount = 0.f;
    float ThunderAmount = 0.f;
    float WindIntensity = 0.f;
    float SnowAmount = 0.f;
    int CurrentPresetIndex = 0;

    // Weather preset names for display
    TArray<FString> PresetNames;
    TArray<FString> PresetAssetNames;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        // Setup preset lists
        PresetNames.Add("Clear Skies");        // 1
        PresetNames.Add("Partly Cloudy");      // 2
        PresetNames.Add("Cloudy");             // 3
        PresetNames.Add("Overcast");           // 4
        PresetNames.Add("Foggy");              // 5
        PresetNames.Add("Light Rain");         // 6
        PresetNames.Add("Rain Thunderstorm");  // 7
        PresetNames.Add("Light Snow");         // 8
        PresetNames.Add("Blizzard");           // 9

        // Auto-find actors
        FindActors();

        if (UDSActor != nullptr)
            Print("WeatherDebugMenu: Ready. Press F1 in-game to open.");
        else
            PrintWarning("WeatherDebugMenu: UDS not found!");
    }

    void FindActors()
    {
        TArray<AActor> AllActors;
        Gameplay::GetAllActorsOfClass(AllActors);
        for (AActor A : AllActors)
        {
            FString ClassName = A.GetClass().GetName().ToString();
            if (UDSActor == nullptr && ClassName.Contains("Ultra_Dynamic_Sky"))
                UDSActor = A;
            if (UDWActor == nullptr && ClassName.Contains("Ultra_Dynamic_Weather"))
                UDWActor = A;
        }
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        // F1 toggle (edge detect)
        bool bF1Down = System::IsKeyDown(EKeys::F1);
        if (bF1Down && !bKeyF1WasDown)
            bMenuVisible = !bMenuVisible;
        bKeyF1WasDown = bF1Down;

        if (!bMenuVisible)
            return;

        // Handle input
        HandleInput(DeltaSeconds);

        // Draw HUD
        DrawDebugHUD();
    }

    void HandleInput(float DeltaSeconds)
    {
        // Time of day: Left/Right
        if (System::IsKeyDown(EKeys::Right))
            AdjustTimeOfDay(50.f * DeltaSeconds);
        if (System::IsKeyDown(EKeys::Left))
            AdjustTimeOfDay(-50.f * DeltaSeconds);

        // Time speed: +/-
        if (System::IsKeyDown(EKeys::Equals))  // + key
            AdjustTimeSpeed(0.5f * DeltaSeconds);
        if (System::IsKeyDown(EKeys::Hyphen))   // - key
            AdjustTimeSpeed(-0.5f * DeltaSeconds);

        // Cloud coverage: Up/Down
        if (System::IsKeyDown(EKeys::Up))
            AdjustCloudCoverage(0.5f * DeltaSeconds);
        if (System::IsKeyDown(EKeys::Down))
            AdjustCloudCoverage(-0.5f * DeltaSeconds);

        // Fog: F/G
        if (System::IsKeyDown(EKeys::F))
            AdjustFog(0.5f * DeltaSeconds);
        if (System::IsKeyDown(EKeys::G))
            AdjustFog(-0.5f * DeltaSeconds);

        // Presets: 1-9
        CheckPresetKeys();

        // Quick jumps
        CheckQuickKeys();
    }

    void CheckPresetKeys()
    {
        if (System::IsKeyDown(EKeys::One)) SetWeatherPreset(0);
        else if (System::IsKeyDown(EKeys::Two)) SetWeatherPreset(1);
        else if (System::IsKeyDown(EKeys::Three)) SetWeatherPreset(2);
        else if (System::IsKeyDown(EKeys::Four)) SetWeatherPreset(3);
        else if (System::IsKeyDown(EKeys::Five)) SetWeatherPreset(4);
        else if (System::IsKeyDown(EKeys::Six)) SetWeatherPreset(5);
        else if (System::IsKeyDown(EKeys::Seven)) SetWeatherPreset(6);
        else if (System::IsKeyDown(EKeys::Eight)) SetWeatherPreset(7);
        else if (System::IsKeyDown(EKeys::Nine)) SetWeatherPreset(8);
    }

    void CheckQuickKeys()
    {
        // N = Night (22:00)
        if (System::IsKeyDown(EKeys::N))
            SetTimeOfDay(2200.f);
        // D = Day (12:00)
        if (System::IsKeyDown(EKeys::D))
            SetTimeOfDay(1200.f);
    }

    // ---- UDS/UDW Property Control ----

    void SetTimeOfDay(float Value)
    {
        TimeOfDay = FMath::Fmod(Value, 2400.f);
        if (TimeOfDay < 0.f) TimeOfDay += 2400.f;
        if (UDSActor != nullptr)
            UDSActor.SetActorPropertyFloat("Time Of Day", TimeOfDay);
    }

    void AdjustTimeOfDay(float Delta)
    {
        SetTimeOfDay(TimeOfDay + Delta);
    }

    void AdjustTimeSpeed(float Delta)
    {
        TimeSpeed = FMath::Clamp(TimeSpeed + Delta, 0.f, 100.f);
        if (UDSActor != nullptr)
        {
            UDSActor.SetActorPropertyFloat("Day Length", FMath::Max(0.01f, 10.f / FMath::Max(TimeSpeed, 0.01f)));
            UDSActor.SetActorPropertyFloat("Night Length", FMath::Max(0.01f, 5.f / FMath::Max(TimeSpeed, 0.01f)));
        }
    }

    void AdjustCloudCoverage(float Delta)
    {
        CloudCoverage = FMath::Clamp(CloudCoverage + Delta, 0.f, 1.f);
        if (UDSActor != nullptr)
            UDSActor.SetActorPropertyFloat("Cloud Coverage", CloudCoverage);
    }

    void AdjustFog(float Delta)
    {
        FogAmount = FMath::Clamp(FogAmount + Delta, 0.f, 1.f);
        if (UDSActor != nullptr)
            UDSActor.SetActorPropertyFloat("Fog", FogAmount);
    }

    void SetWeatherPreset(int Index)
    {
        CurrentPresetIndex = Index;
        // Weather presets need to be applied through UDW
        // For now, log the change
        if (Index < PresetNames.Num())
            Print("WeatherDebugMenu: Switching to " + PresetNames[Index]);
    }

    // ---- Debug HUD Drawing ----

    void DrawDebugHUD()
    {
        // Read current values from UDS if available
        ReadCurrentState();

        FString Header = "===== WEATHER DEBUG [F1 to close] =====";
        FString TimeLine = "Time: " + FormatTime(TimeOfDay) + "  Speed: " + String::Conv_FloatToString(TimeSpeed) + "x";
        FString CloudLine = "Clouds: " + FormatPercent(CloudCoverage) + "  [Up/Down]";
        FString FogLine = "Fog: " + FormatPercent(FogAmount) + "  [F/G]";
        FString RainLine = "Rain: " + FormatPercent(RainAmount) + "  Snow: " + FormatPercent(SnowAmount);
        FString ThunderLine = "Thunder: " + FormatPercent(ThunderAmount) + "  Wind: " + String::Conv_FloatToString(WindIntensity);
        FString PresetLine = "Preset: " + (CurrentPresetIndex < PresetNames.Num() ? PresetNames[CurrentPresetIndex] : "None");
        FString Controls1 = "[Left/Right] Time  [+/-] Speed  [N]ight [D]ay";
        FString Controls2 = "[1-9] Weather Presets  [Up/Down] Clouds  [F/G] Fog";

        float Y = 50.f;
        float Step = 22.f;
        FLinearColor Green = FLinearColor(0.2f, 1.f, 0.2f, 1.f);
        FLinearColor White = FLinearColor(1.f, 1.f, 1.f, 1.f);
        FLinearColor Yellow = FLinearColor(1.f, 1.f, 0.3f, 1.f);
        FLinearColor Gray = FLinearColor(0.6f, 0.6f, 0.6f, 1.f);

        PrintToScreen(Header, Green, Y); Y += Step;
        PrintToScreen("", White, Y); Y += Step * 0.5f;
        PrintToScreen(TimeLine, White, Y); Y += Step;
        PrintToScreen(CloudLine, White, Y); Y += Step;
        PrintToScreen(FogLine, White, Y); Y += Step;
        PrintToScreen(RainLine, White, Y); Y += Step;
        PrintToScreen(ThunderLine, White, Y); Y += Step;
        PrintToScreen("", White, Y); Y += Step * 0.5f;
        PrintToScreen(PresetLine, Yellow, Y); Y += Step;
        PrintToScreen("", White, Y); Y += Step * 0.5f;
        PrintToScreen(Controls1, Gray, Y); Y += Step;
        PrintToScreen(Controls2, Gray, Y); Y += Step;

        // Status
        FString Status = "UDS: " + (UDSActor != nullptr ? "OK" : "MISSING");
        Status += "  UDW: " + (UDWActor != nullptr ? "OK" : "MISSING");
        Y += Step * 0.5f;
        PrintToScreen(Status, UDSActor != nullptr ? Green : FLinearColor(1.f, 0.3f, 0.3f, 1.f), Y);
    }

    void PrintToScreen(FString Text, FLinearColor Color, float Y)
    {
        // Use debug string at screen position
        System::PrintString(Text, true, false, Color, 0.f);
    }

    void ReadCurrentState()
    {
        // TODO: Read actual values from UDS/UDW via property access
        // For now values are tracked from our own sets
    }

    // ---- Formatting Helpers ----

    FString FormatTime(float TOD)
    {
        int Hours = FMath::FloorToInt(TOD / 100.f);
        int Minutes = FMath::FloorToInt(FMath::Fmod(TOD, 100.f) * 0.6f);
        return "" + Hours + ":" + (Minutes < 10 ? "0" : "") + Minutes;
    }

    FString FormatPercent(float Value)
    {
        int Pct = FMath::RoundToInt(Value * 100.f);
        return "" + Pct + "%";
    }
}
