// WeatherDebugMenu.cpp
#include "Runtime/WeatherDebugMenu.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogWeatherDebug, Log, All);

// ---- Preset Names ----
static const TArray<FString> GPresetNames = {
	TEXT("Clear Skies"),         // 1
	TEXT("Partly Cloudy"),       // 2
	TEXT("Cloudy"),              // 3
	TEXT("Overcast"),            // 4
	TEXT("Foggy"),               // 5
	TEXT("Light Rain"),          // 6
	TEXT("Rain/Thunderstorm"),   // 7
	TEXT("Light Snow"),          // 8
	TEXT("Blizzard"),            // 9
};

const TArray<FString>& AWeatherDebugMenu::GetPresetNames()
{
	return GPresetNames;
}

// ---- Constructor ----
AWeatherDebugMenu::AWeatherDebugMenu()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

// ---- BeginPlay ----
void AWeatherDebugMenu::BeginPlay()
{
	Super::BeginPlay();

	FindUDS();
	FindUDW();

	UE_LOG(LogWeatherDebug, Log, TEXT("===== WeatherDebugMenu START ====="));
	UE_LOG(LogWeatherDebug, Log, TEXT("UDS: %s"), UDSActor ? *UDSActor->GetName() : TEXT("NOT FOUND"));
	UE_LOG(LogWeatherDebug, Log, TEXT("UDW: %s"), UDWActor ? *UDWActor->GetName() : TEXT("NOT FOUND"));
	UE_LOG(LogWeatherDebug, Log, TEXT("Press F9 to toggle debug menu"));
	UE_LOG(LogWeatherDebug, Log, TEXT("===== WeatherDebugMenu READY ====="));
}

// ---- Tick ----
void AWeatherDebugMenu::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;

	// Toggle menu with F1 (edge detect)
	const bool bToggleDown = PC->IsInputKeyDown(ToggleKey);
	if (bToggleDown && !bToggleKeyWasDown)
	{
		bMenuVisible = !bMenuVisible;
	}
	bToggleKeyWasDown = bToggleDown;

	if (!bMenuVisible) return;

	HandleInput(DeltaSeconds);
	DrawHUD();
}

// ---- Actor Discovery ----
void AWeatherDebugMenu::FindUDS()
{
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AActor::StaticClass(), Found);
	for (AActor* A : Found)
	{
		if (A && A->GetClass()->GetName().Contains(TEXT("Ultra_Dynamic_Sky")))
		{
			UDSActor = A;
			return;
		}
	}
}

void AWeatherDebugMenu::FindUDW()
{
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AActor::StaticClass(), Found);
	for (AActor* A : Found)
	{
		if (A && A->GetClass()->GetName().Contains(TEXT("Ultra_Dynamic_Weather")))
		{
			UDWActor = A;
			return;
		}
	}
}

// ---- Input ----
bool AWeatherDebugMenu::WasKeyJustPressed(FKey Key)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return false;

	const bool bDown = PC->IsInputKeyDown(Key);
	const bool bWasDown = KeysDownLastFrame.Contains(Key);

	if (bDown && !bWasDown)
	{
		KeysDownLastFrame.Add(Key);
		return true;
	}
	if (!bDown && bWasDown)
	{
		KeysDownLastFrame.Remove(Key);
	}
	return false;
}

void AWeatherDebugMenu::HandleInput(float DeltaSeconds)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;

	// ---- Time of Day: Left/Right (hold) ----
	if (PC->IsInputKeyDown(EKeys::Right))
	{
		TimeOfDayValue += TimeAdjustSpeed * DeltaSeconds;
		if (TimeOfDayValue >= 2400.f) TimeOfDayValue -= 2400.f;
		SetTimeOfDay(TimeOfDayValue);
	}
	if (PC->IsInputKeyDown(EKeys::Left))
	{
		TimeOfDayValue -= TimeAdjustSpeed * DeltaSeconds;
		if (TimeOfDayValue < 0.f) TimeOfDayValue += 2400.f;
		SetTimeOfDay(TimeOfDayValue);
	}

	// ---- Day/Night Speed: +/- (press) ----
	if (WasKeyJustPressed(EKeys::Equals)) // +
	{
		DayLengthValue = FMath::Max(0.01f, DayLengthValue * 0.5f);
		NightLengthValue = FMath::Max(0.01f, NightLengthValue * 0.5f);
		SetDayLength(DayLengthValue);
		SetNightLength(NightLengthValue);
	}
	if (WasKeyJustPressed(EKeys::Hyphen)) // -
	{
		DayLengthValue = FMath::Min(60.f, DayLengthValue * 2.f);
		NightLengthValue = FMath::Min(60.f, NightLengthValue * 2.f);
		SetDayLength(DayLengthValue);
		SetNightLength(NightLengthValue);
	}

	// ---- Cloud Coverage: Up/Down (hold) ----
	if (PC->IsInputKeyDown(EKeys::Up))
	{
		CloudCoverageValue = FMath::Clamp(CloudCoverageValue + 0.5f * DeltaSeconds, 0.f, 1.f);
		SetCloudCoverage(CloudCoverageValue);
	}
	if (PC->IsInputKeyDown(EKeys::Down))
	{
		CloudCoverageValue = FMath::Clamp(CloudCoverageValue - 0.5f * DeltaSeconds, 0.f, 1.f);
		SetCloudCoverage(CloudCoverageValue);
	}

	// ---- Fog: F increase, G decrease (hold) ----
	if (PC->IsInputKeyDown(EKeys::F))
	{
		FogValue = FMath::Clamp(FogValue + 0.5f * DeltaSeconds, 0.f, 1.f);
		SetFog(FogValue);
	}
	if (PC->IsInputKeyDown(EKeys::G))
	{
		FogValue = FMath::Clamp(FogValue - 0.5f * DeltaSeconds, 0.f, 1.f);
		SetFog(FogValue);
	}

	// ---- Quick jumps: N=Night, D=Day ----
	if (WasKeyJustPressed(EKeys::N))
	{
		TimeOfDayValue = 2200.f;
		SetTimeOfDay(TimeOfDayValue);
	}
	if (WasKeyJustPressed(EKeys::D))
	{
		TimeOfDayValue = 1200.f;
		SetTimeOfDay(TimeOfDayValue);
	}

	// ---- Animate toggle: P ----
	if (WasKeyJustPressed(EKeys::P))
	{
		bAnimateTime = !bAnimateTime;
		if (UDSActor)
			SetBoolOnActor(UDSActor, FName("Animate Time of Day"), bAnimateTime);
	}

	// ---- Weather Presets: 1-9 ----
	const FKey NumKeys[] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine
	};
	for (int32 i = 0; i < 9; ++i)
	{
		if (WasKeyJustPressed(NumKeys[i]))
		{
			SetWeatherPreset(i);
		}
	}

	// ---- Read current state from UDS ----
	if (UDSActor)
	{
		TimeOfDayValue = GetFloatFromActor(UDSActor, FName("Time Of Day"));
	}
}

// ---- UDS/UDW Property Access via Reflection ----

void AWeatherDebugMenu::SetFloatOnActor(AActor* Actor, FName PropertyName, float Value)
{
	if (!Actor) return;

	FProperty* Prop = Actor->GetClass()->FindPropertyByName(PropertyName);
	if (!Prop)
	{
		UE_LOG(LogWeatherDebug, Warning, TEXT("Property '%s' not found on %s"), *PropertyName.ToString(), *Actor->GetName());
		return;
	}

	if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
	{
		FloatProp->SetPropertyValue_InContainer(Actor, Value);
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
	{
		DoubleProp->SetPropertyValue_InContainer(Actor, (double)Value);
	}
}

float AWeatherDebugMenu::GetFloatFromActor(AActor* Actor, FName PropertyName) const
{
	if (!Actor) return 0.f;

	FProperty* Prop = Actor->GetClass()->FindPropertyByName(PropertyName);
	if (!Prop) return 0.f;

	if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
	{
		return FloatProp->GetPropertyValue_InContainer(Actor);
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
	{
		return (float)DoubleProp->GetPropertyValue_InContainer(Actor);
	}
	return 0.f;
}

void AWeatherDebugMenu::SetBoolOnActor(AActor* Actor, FName PropertyName, bool Value)
{
	if (!Actor) return;

	FBoolProperty* BoolProp = CastField<FBoolProperty>(Actor->GetClass()->FindPropertyByName(PropertyName));
	if (BoolProp)
	{
		BoolProp->SetPropertyValue_InContainer(Actor, Value);
	}
}

void AWeatherDebugMenu::CallChangeWeather(int32 PresetIndex)
{
	// Weather preset switching via UDW's ChangeWeather function
	// TODO: Load the preset asset and call the function via reflection
	UE_LOG(LogWeatherDebug, Log, TEXT("Weather preset %d: %s"), PresetIndex,
		PresetIndex < GetPresetNames().Num() ? *GetPresetNames()[PresetIndex] : TEXT("Unknown"));
}

// ---- Public API ----

void AWeatherDebugMenu::SetTimeOfDay(float Time)
{
	TimeOfDayValue = FMath::Fmod(Time, 2400.f);
	if (TimeOfDayValue < 0.f) TimeOfDayValue += 2400.f;
	SetFloatOnActor(UDSActor, FName("Time Of Day"), TimeOfDayValue);
}

void AWeatherDebugMenu::SetCloudCoverage(float Coverage)
{
	CloudCoverageValue = FMath::Clamp(Coverage, 0.f, 1.f);
	SetFloatOnActor(UDSActor, FName("Cloud Coverage"), CloudCoverageValue);
}

void AWeatherDebugMenu::SetFog(float Fog)
{
	FogValue = FMath::Clamp(Fog, 0.f, 1.f);
	SetFloatOnActor(UDSActor, FName("Fog"), FogValue);
}

void AWeatherDebugMenu::SetWeatherPreset(int32 Index)
{
	CurrentPresetIndex = Index;
	CallChangeWeather(Index);
}

void AWeatherDebugMenu::SetDayLength(float Minutes)
{
	DayLengthValue = Minutes;
	SetFloatOnActor(UDSActor, FName("Day Length"), Minutes);
}

void AWeatherDebugMenu::SetNightLength(float Minutes)
{
	NightLengthValue = Minutes;
	SetFloatOnActor(UDSActor, FName("Night Length"), Minutes);
}

// ---- HUD Drawing ----

void AWeatherDebugMenu::DrawLine(const FString& Text, FColor Color, float& Y)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 0.f, Color, Text);
	}
	Y += 20.f;
}

void AWeatherDebugMenu::DrawHUD()
{
	if (!GEngine) return;

	// Format time
	const int32 Hours = FMath::FloorToInt32(TimeOfDayValue / 100.f);
	const int32 Minutes = FMath::FloorToInt32(FMath::Fmod(TimeOfDayValue, 100.f) * 0.6f);
	FString TimeStr = FString::Printf(TEXT("%02d:%02d"), Hours, Minutes);

	// Speed display
	float SpeedMultiplier = (DayLengthValue > 0.01f) ? (10.f / DayLengthValue) : 999.f;

	// Preset name
	FString PresetStr = (CurrentPresetIndex >= 0 && CurrentPresetIndex < GetPresetNames().Num())
		? GetPresetNames()[CurrentPresetIndex]
		: TEXT("(default)");

	// Build display
	const FColor Green(100, 255, 100);
	const FColor White(255, 255, 255);
	const FColor Yellow(255, 255, 80);
	const FColor Gray(160, 160, 160);
	const FColor Cyan(100, 255, 255);
	const FColor Red(255, 100, 100);

	float Y = 0.f;

	// Draw from bottom up since AddOnScreenDebugMessage stacks
	DrawLine(TEXT(""), White, Y);
	DrawLine(FString::Printf(TEXT("  UDS: %s  |  UDW: %s"),
		UDSActor ? TEXT("OK") : TEXT("MISSING"),
		UDWActor ? TEXT("OK") : TEXT("MISSING")),
		UDSActor ? Green : Red, Y);
	DrawLine(TEXT(""), White, Y);
	DrawLine(TEXT("  [1-9] Presets  [P] Pause/Play  [N]ight [D]ay"), Gray, Y);
	DrawLine(TEXT("  [Left/Right] Time  [+/-] Speed  [Up/Down] Clouds  [F/G] Fog"), Gray, Y);
	DrawLine(TEXT(""), White, Y);
	DrawLine(FString::Printf(TEXT("  Preset: %s"), *PresetStr), Yellow, Y);
	DrawLine(FString::Printf(TEXT("  Rain: %.0f%%  Snow: %.0f%%  Thunder: %.0f%%"),
		RainValue * 100.f, SnowValue * 100.f, ThunderValue * 100.f), White, Y);
	DrawLine(FString::Printf(TEXT("  Clouds: %.0f%%  Fog: %.0f%%"),
		CloudCoverageValue * 100.f, FogValue * 100.f), White, Y);
	DrawLine(FString::Printf(TEXT("  Time: %s  Speed: %.1fx  %s"),
		*TimeStr, SpeedMultiplier, bAnimateTime ? TEXT("[Playing]") : TEXT("[Paused]")), Cyan, Y);
	DrawLine(TEXT(""), White, Y);
	DrawLine(TEXT("===== WEATHER DEBUG [F1] ====="), Green, Y);
}
