// WeatherDebugMenu.h - UMG-based in-game weather debug panel
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeatherDebugMenu.generated.h"

class UComboBoxString;
class USlider;
class UTextBlock;
class UButton;
class UCheckBox;

/**
 * Concrete UserWidget subclass for the weather debug panel.
 * Needed because UUserWidget is abstract and cannot be instantiated directly.
 */
UCLASS()
class GISPROCEDURAL_API UWeatherDebugWidget : public UUserWidget
{
	GENERATED_BODY()
};

/**
 * In-game weather debug menu with UMG UI.
 *
 * Drop into level, press Home to toggle a clean panel with:
 * - Weather preset dropdown
 * - Time of day slider
 * - Cloud/Fog/Rain/Snow/Thunder sliders
 * - Day/Night cycle speed control
 * - Quick jump buttons
 *
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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Key to toggle the debug menu (default: Home) */
	UPROPERTY(EditAnywhere, Category = "Weather Debug")
	FKey ToggleKey = EKeys::Home;

private:
	// ---- UI ----
	void CreateUI();
	void RemoveUI();
	void UpdateUIValues();

	// ---- Actor Discovery ----
	void FindUDS();
	void FindUDW();

	// ---- UDS/UDW Reflection Helpers ----
	void SetFloatProp(AActor* Actor, FName Name, float Value);
	float GetFloatProp(AActor* Actor, FName Name) const;
	void SetBoolProp(AActor* Actor, FName Name, bool Value);

	// ---- Callbacks ----
	UFUNCTION() void OnPresetChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void OnTimeSliderChanged(float Value);
	UFUNCTION() void OnCloudSliderChanged(float Value);
	UFUNCTION() void OnFogSliderChanged(float Value);
	UFUNCTION() void OnRainSliderChanged(float Value);
	UFUNCTION() void OnSnowSliderChanged(float Value);
	UFUNCTION() void OnThunderSliderChanged(float Value);
	UFUNCTION() void OnDaySpeedSliderChanged(float Value);
	UFUNCTION() void OnNightSpeedSliderChanged(float Value);
	UFUNCTION() void OnDayClicked();
	UFUNCTION() void OnNightClicked();
	UFUNCTION() void OnAnimateChanged(bool bIsChecked);

	// ---- State ----
	UPROPERTY() TObjectPtr<AActor> UDSActor;
	UPROPERTY() TObjectPtr<AActor> UDWActor;
	UPROPERTY() TObjectPtr<UWeatherDebugWidget> MenuWidget;

	bool bMenuVisible = false;
	bool bToggleWasDown = false;
	bool bUICreated = false;

	// Widget references
	UPROPERTY() UComboBoxString* PresetCombo = nullptr;
	UPROPERTY() USlider* TimeSlider = nullptr;
	UPROPERTY() USlider* CloudSlider = nullptr;
	UPROPERTY() USlider* FogSlider = nullptr;
	UPROPERTY() USlider* RainSlider = nullptr;
	UPROPERTY() USlider* SnowSlider = nullptr;
	UPROPERTY() USlider* ThunderSlider = nullptr;
	UPROPERTY() USlider* DaySpeedSlider = nullptr;
	UPROPERTY() USlider* NightSpeedSlider = nullptr;
	UPROPERTY() UTextBlock* TimeLabel = nullptr;
	UPROPERTY() UTextBlock* StatusLabel = nullptr;
	UPROPERTY() UCheckBox* AnimateCheck = nullptr;
};
