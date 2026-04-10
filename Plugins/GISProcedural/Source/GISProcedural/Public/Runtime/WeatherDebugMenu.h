// WeatherDebugMenu.h - In-game debug HUD for UDS/UDW weather control
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeatherDebugMenu.generated.h"

/**
 * In-game weather debug menu.
 *
 * Drop into level, press F1 to toggle.
 * Controls time-of-day, weather presets, cloud/fog/rain/thunder.
 * Auto-discovers Ultra Dynamic Sky and Ultra Dynamic Weather actors.
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API AWeatherDebugMenu : public AActor
{
	GENERATED_BODY()

public:
	AWeatherDebugMenu();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- Configuration ----

	/** Key to toggle the debug menu */
	UPROPERTY(EditAnywhere, Category = "Weather Debug")
	FKey ToggleKey = EKeys::F9;

	/** Time adjustment step per second when holding arrow keys */
	UPROPERTY(EditAnywhere, Category = "Weather Debug")
	float TimeAdjustSpeed = 200.f;

	// ---- Public API ----

	UFUNCTION(BlueprintCallable, Category = "Weather Debug")
	void SetTimeOfDay(float Time);

	UFUNCTION(BlueprintCallable, Category = "Weather Debug")
	void SetCloudCoverage(float Coverage);

	UFUNCTION(BlueprintCallable, Category = "Weather Debug")
	void SetFog(float Fog);

	UFUNCTION(BlueprintCallable, Category = "Weather Debug")
	void SetWeatherPreset(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Weather Debug")
	void SetDayLength(float Minutes);

	UFUNCTION(BlueprintCallable, Category = "Weather Debug")
	void SetNightLength(float Minutes);

private:
	// ---- Actor Discovery ----
	void FindUDS();
	void FindUDW();

	// ---- Input ----
	void HandleInput(float DeltaSeconds);
	bool WasKeyJustPressed(FKey Key);

	// ---- UDS/UDW Property Access (reflection) ----
	void SetFloatOnActor(AActor* Actor, FName PropertyName, float Value);
	float GetFloatFromActor(AActor* Actor, FName PropertyName) const;
	void SetBoolOnActor(AActor* Actor, FName PropertyName, bool Value);
	void CallChangeWeather(int32 PresetIndex);

	// ---- HUD Drawing ----
	void DrawHUD();
	void DrawLine(const FString& Text, FColor Color, float& Y);

	// ---- State ----
	UPROPERTY()
	TObjectPtr<AActor> UDSActor;

	UPROPERTY()
	TObjectPtr<AActor> UDWActor;

	bool bMenuVisible = false;
	bool bToggleKeyWasDown = false;
	TMap<FKey, bool> PrevKeyStates;

	// Tracked weather values
	float TimeOfDayValue = 1200.f;
	float DayLengthValue = 2.f;
	float NightLengthValue = 1.f;
	float CloudCoverageValue = 0.5f;
	float FogValue = 0.f;
	float RainValue = 0.f;
	float ThunderValue = 0.f;
	float SnowValue = 0.f;
	int32 CurrentPresetIndex = -1;
	bool bAnimateTime = true;

	// Preset info
	static const TArray<FString>& GetPresetNames();

	// Key state tracking for edge detection
	TSet<FKey> KeysDownLastFrame;
};
