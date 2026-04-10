// WeatherDebugMenu.cpp
#include "Runtime/WeatherDebugMenu.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"

DEFINE_LOG_CATEGORY_STATIC(LogWeatherDebug, Log, All);

// ---- Presets ----
static const TArray<FString> GWeatherPresets = {
	TEXT("Clear Skies"),
	TEXT("Partly Cloudy"),
	TEXT("Cloudy"),
	TEXT("Overcast"),
	TEXT("Foggy"),
	TEXT("Light Rain"),
	TEXT("Rain / Thunderstorm"),
	TEXT("Light Snow"),
	TEXT("Blizzard"),
};

// Helper: create a labeled slider row
static UHorizontalBox* MakeSliderRow(
	UWidgetTree* Tree,
	const FString& LabelText,
	USlider*& OutSlider,
	UTextBlock*& OutValueLabel,
	float InitialValue = 0.5f)
{
	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();

	// Label
	UTextBlock* Label = Tree->ConstructWidget<UTextBlock>();
	Label->SetText(FText::FromString(LabelText));
	FSlateFontInfo Font = Label->GetFont();
	Font.Size = 11;
	Label->SetFont(Font);
	Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	auto* LabelSlot = Row->AddChildToHorizontalBox(Label);
	LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LabelSlot->SetVerticalAlignment(VAlign_Center);
	LabelSlot->SetPadding(FMargin(0, 0, 8, 0));

	// Slider
	OutSlider = Tree->ConstructWidget<USlider>();
	OutSlider->SetValue(InitialValue);
	OutSlider->SetMinValue(0.f);
	OutSlider->SetMaxValue(1.f);
	OutSlider->SetStepSize(0.01f);
	OutSlider->SetSliderBarColor(FLinearColor(0.2f, 0.2f, 0.2f));
	OutSlider->SetSliderHandleColor(FLinearColor(0.0f, 0.7f, 1.0f));
	auto* SliderSlot = Row->AddChildToHorizontalBox(OutSlider);
	SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SliderSlot->SetVerticalAlignment(VAlign_Center);

	// Value text
	OutValueLabel = Tree->ConstructWidget<UTextBlock>();
	OutValueLabel->SetText(FText::FromString(TEXT("50%")));
	FSlateFontInfo VFont = OutValueLabel->GetFont();
	VFont.Size = 10;
	OutValueLabel->SetFont(VFont);
	OutValueLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)));
	auto* ValSlot = Row->AddChildToHorizontalBox(OutValueLabel);
	ValSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	ValSlot->SetVerticalAlignment(VAlign_Center);
	ValSlot->SetPadding(FMargin(8, 0, 0, 0));

	return Row;
}

// ---- Constructor ----
AWeatherDebugMenu::AWeatherDebugMenu()
{
	PrimaryActorTick.bCanEverTick = true;
}

// ---- BeginPlay ----
void AWeatherDebugMenu::BeginPlay()
{
	Super::BeginPlay();
	FindUDS();
	FindUDW();

	UE_LOG(LogWeatherDebug, Log, TEXT("===== WeatherDebugMenu ====="));
	UE_LOG(LogWeatherDebug, Log, TEXT("UDS: %s"), UDSActor ? *UDSActor->GetName() : TEXT("NOT FOUND"));
	UE_LOG(LogWeatherDebug, Log, TEXT("UDW: %s"), UDWActor ? *UDWActor->GetName() : TEXT("NOT FOUND"));
	UE_LOG(LogWeatherDebug, Log, TEXT("Press F9 to toggle"));
}

// ---- EndPlay ----
void AWeatherDebugMenu::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveUI();
	Super::EndPlay(EndPlayReason);
}

// ---- Tick ----
void AWeatherDebugMenu::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return;

	// Toggle
	const bool bDown = PC->IsInputKeyDown(ToggleKey);
	if (bDown && !bToggleWasDown)
	{
		bMenuVisible = !bMenuVisible;
		if (bMenuVisible)
		{
			if (!bUICreated) CreateUI();
			if (MenuWidget) MenuWidget->SetVisibility(ESlateVisibility::Visible);
			PC->SetShowMouseCursor(true);
			PC->SetInputMode(FInputModeGameAndUI());
		}
		else
		{
			if (MenuWidget) MenuWidget->SetVisibility(ESlateVisibility::Collapsed);
			PC->SetShowMouseCursor(false);
			PC->SetInputMode(FInputModeGameOnly());
		}
	}
	bToggleWasDown = bDown;

	// Update display
	if (bMenuVisible && bUICreated)
	{
		UpdateUIValues();
	}
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

// ---- Create UI ----
void AWeatherDebugMenu::CreateUI()
{
	if (bUICreated) return;

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;

	// Create empty UserWidget
	MenuWidget = CreateWidget<UUserWidget>(PC);
	if (!MenuWidget) return;

	UWidgetTree* Tree = MenuWidget->WidgetTree;

	// Root canvas
	UCanvasPanel* Canvas = Tree->ConstructWidget<UCanvasPanel>();
	Tree->RootWidget = Canvas;

	// Background border
	UBorder* BG = Tree->ConstructWidget<UBorder>();
	BG->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.05f, 0.9f));
	BG->SetPadding(FMargin(16.f));
	auto* BGSlot = Canvas->AddChildToCanvas(BG);
	BGSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
	BGSlot->SetPosition(FVector2D(30.f, 80.f));
	BGSlot->SetSize(FVector2D(380.f, 620.f));

	// Main vertical layout
	UVerticalBox* VBox = Tree->ConstructWidget<UVerticalBox>();
	BG->AddChild(VBox);

	// ---- Title ----
	{
		UTextBlock* Title = Tree->ConstructWidget<UTextBlock>();
		Title->SetText(FText::FromString(TEXT("Weather Debug Panel")));
		FSlateFontInfo TFont = Title->GetFont();
		TFont.Size = 16;
		Title->SetFont(TFont);
		Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.2f, 0.8f, 1.0f)));
		auto* Slot = VBox->AddChildToVerticalBox(Title);
		Slot->SetPadding(FMargin(0, 0, 0, 12));
	}

	// ---- Status ----
	{
		StatusLabel = Tree->ConstructWidget<UTextBlock>();
		StatusLabel->SetText(FText::FromString(TEXT("Status: ...")));
		FSlateFontInfo SFont = StatusLabel->GetFont();
		SFont.Size = 9;
		StatusLabel->SetFont(SFont);
		StatusLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.5f, 1.0f, 0.5f)));
		auto* Slot = VBox->AddChildToVerticalBox(StatusLabel);
		Slot->SetPadding(FMargin(0, 0, 0, 8));
	}

	// ---- Weather Preset Dropdown ----
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(TEXT("Weather Preset")));
		FSlateFontInfo F = Label->GetFont();
		F.Size = 11;
		Label->SetFont(F);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		auto* LSlot = VBox->AddChildToVerticalBox(Label);
		LSlot->SetPadding(FMargin(0, 4, 0, 2));

		PresetCombo = Tree->ConstructWidget<UComboBoxString>();
		for (const FString& P : GWeatherPresets)
		{
			PresetCombo->AddOption(P);
		}
		PresetCombo->SetSelectedOption(GWeatherPresets[0]);
		PresetCombo->OnSelectionChanged.AddDynamic(this, &AWeatherDebugMenu::OnPresetChanged);
		auto* CSlot = VBox->AddChildToVerticalBox(PresetCombo);
		CSlot->SetPadding(FMargin(0, 0, 0, 8));
	}

	// ---- Time of Day ----
	{
		UTextBlock* ValLabel = nullptr;
		UHorizontalBox* Row = MakeSliderRow(Tree, TEXT("Time of Day"), TimeSlider, ValLabel, 0.5f);
		TimeLabel = ValLabel;
		TimeSlider->OnValueChanged.AddDynamic(this, &AWeatherDebugMenu::OnTimeSliderChanged);
		auto* Slot = VBox->AddChildToVerticalBox(Row);
		Slot->SetPadding(FMargin(0, 2, 0, 2));
	}

	// ---- Animate Time ----
	{
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(TEXT("Animate Time")));
		FSlateFontInfo F = Label->GetFont();
		F.Size = 11;
		Label->SetFont(F);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		auto* LSlot = Row->AddChildToHorizontalBox(Label);
		LSlot->SetVerticalAlignment(VAlign_Center);
		LSlot->SetPadding(FMargin(0, 0, 8, 0));

		AnimateCheck = Tree->ConstructWidget<UCheckBox>();
		AnimateCheck->SetIsChecked(true);
		AnimateCheck->OnCheckStateChanged.AddDynamic(this, &AWeatherDebugMenu::OnAnimateChanged);
		Row->AddChildToHorizontalBox(AnimateCheck);

		auto* Slot = VBox->AddChildToVerticalBox(Row);
		Slot->SetPadding(FMargin(0, 2, 0, 4));
	}

	// ---- Day/Night Speed ----
	{
		UTextBlock* ValLabel = nullptr;
		UHorizontalBox* Row = MakeSliderRow(Tree, TEXT("Day Length (min)"), DaySpeedSlider, ValLabel, 0.1f);
		DaySpeedSlider->OnValueChanged.AddDynamic(this, &AWeatherDebugMenu::OnDaySpeedSliderChanged);
		auto* Slot = VBox->AddChildToVerticalBox(Row);
		Slot->SetPadding(FMargin(0, 2, 0, 2));
	}
	{
		UTextBlock* ValLabel = nullptr;
		UHorizontalBox* Row = MakeSliderRow(Tree, TEXT("Night Length (min)"), NightSpeedSlider, ValLabel, 0.1f);
		NightSpeedSlider->OnValueChanged.AddDynamic(this, &AWeatherDebugMenu::OnNightSpeedSliderChanged);
		auto* Slot = VBox->AddChildToVerticalBox(Row);
		Slot->SetPadding(FMargin(0, 2, 0, 4));
	}

	// ---- Cloud Coverage ----
	{
		UTextBlock* ValLabel = nullptr;
		UHorizontalBox* Row = MakeSliderRow(Tree, TEXT("Cloud Coverage"), CloudSlider, ValLabel, 0.5f);
		CloudSlider->OnValueChanged.AddDynamic(this, &AWeatherDebugMenu::OnCloudSliderChanged);
		auto* Slot = VBox->AddChildToVerticalBox(Row);
		Slot->SetPadding(FMargin(0, 2, 0, 2));
	}

	// ---- Fog ----
	{
		UTextBlock* ValLabel = nullptr;
		UHorizontalBox* Row = MakeSliderRow(Tree, TEXT("Fog"), FogSlider, ValLabel, 0.0f);
		FogSlider->OnValueChanged.AddDynamic(this, &AWeatherDebugMenu::OnFogSliderChanged);
		auto* Slot = VBox->AddChildToVerticalBox(Row);
		Slot->SetPadding(FMargin(0, 2, 0, 2));
	}

	// ---- Rain ----
	{
		UTextBlock* ValLabel = nullptr;
		UHorizontalBox* Row = MakeSliderRow(Tree, TEXT("Rain"), RainSlider, ValLabel, 0.0f);
		RainSlider->OnValueChanged.AddDynamic(this, &AWeatherDebugMenu::OnRainSliderChanged);
		auto* Slot = VBox->AddChildToVerticalBox(Row);
		Slot->SetPadding(FMargin(0, 2, 0, 2));
	}

	// ---- Snow ----
	{
		UTextBlock* ValLabel = nullptr;
		UHorizontalBox* Row = MakeSliderRow(Tree, TEXT("Snow"), SnowSlider, ValLabel, 0.0f);
		SnowSlider->OnValueChanged.AddDynamic(this, &AWeatherDebugMenu::OnSnowSliderChanged);
		auto* Slot = VBox->AddChildToVerticalBox(Row);
		Slot->SetPadding(FMargin(0, 2, 0, 2));
	}

	// ---- Thunder ----
	{
		UTextBlock* ValLabel = nullptr;
		UHorizontalBox* Row = MakeSliderRow(Tree, TEXT("Thunder"), ThunderSlider, ValLabel, 0.0f);
		ThunderSlider->OnValueChanged.AddDynamic(this, &AWeatherDebugMenu::OnThunderSliderChanged);
		auto* Slot = VBox->AddChildToVerticalBox(Row);
		Slot->SetPadding(FMargin(0, 2, 0, 8));
	}

	// ---- Quick Buttons ----
	{
		UHorizontalBox* BtnRow = Tree->ConstructWidget<UHorizontalBox>();

		// Day button
		UButton* DayBtn = Tree->ConstructWidget<UButton>();
		DayBtn->OnClicked.AddDynamic(this, &AWeatherDebugMenu::OnDayClicked);
		UTextBlock* DayText = Tree->ConstructWidget<UTextBlock>();
		DayText->SetText(FText::FromString(TEXT("Jump to Day")));
		FSlateFontInfo BF = DayText->GetFont();
		BF.Size = 11;
		DayText->SetFont(BF);
		DayBtn->AddChild(DayText);
		auto* DSlot = BtnRow->AddChildToHorizontalBox(DayBtn);
		DSlot->SetPadding(FMargin(0, 0, 8, 0));

		// Night button
		UButton* NightBtn = Tree->ConstructWidget<UButton>();
		NightBtn->OnClicked.AddDynamic(this, &AWeatherDebugMenu::OnNightClicked);
		UTextBlock* NightText = Tree->ConstructWidget<UTextBlock>();
		NightText->SetText(FText::FromString(TEXT("Jump to Night")));
		NightText->SetFont(BF);
		NightBtn->AddChild(NightText);
		BtnRow->AddChildToHorizontalBox(NightBtn);

		auto* Slot = VBox->AddChildToVerticalBox(BtnRow);
		Slot->SetPadding(FMargin(0, 4, 0, 0));
	}

	MenuWidget->AddToViewport(100);
	MenuWidget->SetVisibility(ESlateVisibility::Collapsed);
	bUICreated = true;
}

void AWeatherDebugMenu::RemoveUI()
{
	if (MenuWidget)
	{
		MenuWidget->RemoveFromParent();
		MenuWidget = nullptr;
	}
	bUICreated = false;
}

// ---- Update display values from UDS ----
void AWeatherDebugMenu::UpdateUIValues()
{
	if (!UDSActor) return;

	float TOD = GetFloatProp(UDSActor, FName("Time Of Day"));
	if (TimeLabel)
	{
		int32 H = FMath::FloorToInt32(TOD / 100.f);
		int32 M = FMath::FloorToInt32(FMath::Fmod(TOD, 100.f) * 0.6f);
		TimeLabel->SetText(FText::FromString(FString::Printf(TEXT("%02d:%02d"), H, M)));
	}

	// Update status
	if (StatusLabel)
	{
		StatusLabel->SetText(FText::FromString(FString::Printf(
			TEXT("UDS: %s | UDW: %s"),
			UDSActor ? TEXT("OK") : TEXT("--"),
			UDWActor ? TEXT("OK") : TEXT("--")
		)));
	}
}

// ---- Reflection Helpers ----
void AWeatherDebugMenu::SetFloatProp(AActor* Actor, FName Name, float Value)
{
	if (!Actor) return;
	FProperty* P = Actor->GetClass()->FindPropertyByName(Name);
	if (!P)
	{
		UE_LOG(LogWeatherDebug, Warning, TEXT("Prop '%s' not found on %s"), *Name.ToString(), *Actor->GetName());
		return;
	}
	if (FFloatProperty* FP = CastField<FFloatProperty>(P))
		FP->SetPropertyValue_InContainer(Actor, Value);
	else if (FDoubleProperty* DP = CastField<FDoubleProperty>(P))
		DP->SetPropertyValue_InContainer(Actor, (double)Value);
}

float AWeatherDebugMenu::GetFloatProp(AActor* Actor, FName Name) const
{
	if (!Actor) return 0.f;
	FProperty* P = Actor->GetClass()->FindPropertyByName(Name);
	if (!P) return 0.f;
	if (FFloatProperty* FP = CastField<FFloatProperty>(P))
		return FP->GetPropertyValue_InContainer(Actor);
	if (FDoubleProperty* DP = CastField<FDoubleProperty>(P))
		return (float)DP->GetPropertyValue_InContainer(Actor);
	return 0.f;
}

void AWeatherDebugMenu::SetBoolProp(AActor* Actor, FName Name, bool Value)
{
	if (!Actor) return;
	FBoolProperty* BP = CastField<FBoolProperty>(Actor->GetClass()->FindPropertyByName(Name));
	if (BP)
		BP->SetPropertyValue_InContainer(Actor, Value);
}

// ---- Callbacks ----
void AWeatherDebugMenu::OnPresetChanged(FString Item, ESelectInfo::Type SelectionType)
{
	// Map preset name to UDS/UDW values
	int32 Idx = GWeatherPresets.IndexOfByKey(Item);
	UE_LOG(LogWeatherDebug, Log, TEXT("Preset: %s (%d)"), *Item, Idx);

	// Apply basic weather parameters based on preset
	struct FPresetData { float Cloud; float Fog; float Rain; float Snow; float Thunder; };
	static const FPresetData Presets[] = {
		{0.0f, 0.0f, 0.0f, 0.0f, 0.0f},  // Clear
		{0.3f, 0.0f, 0.0f, 0.0f, 0.0f},  // Partly Cloudy
		{0.6f, 0.0f, 0.0f, 0.0f, 0.0f},  // Cloudy
		{0.9f, 0.1f, 0.0f, 0.0f, 0.0f},  // Overcast
		{0.5f, 0.8f, 0.0f, 0.0f, 0.0f},  // Foggy
		{0.7f, 0.1f, 0.5f, 0.0f, 0.0f},  // Light Rain
		{1.0f, 0.2f, 1.0f, 0.0f, 1.0f},  // Rain/Thunderstorm
		{0.6f, 0.1f, 0.0f, 0.5f, 0.0f},  // Light Snow
		{1.0f, 0.3f, 0.0f, 1.0f, 0.0f},  // Blizzard
	};

	if (Idx >= 0 && Idx < 9)
	{
		const auto& P = Presets[Idx];
		OnCloudSliderChanged(P.Cloud);
		OnFogSliderChanged(P.Fog);
		OnRainSliderChanged(P.Rain);
		OnSnowSliderChanged(P.Snow);
		OnThunderSliderChanged(P.Thunder);

		// Update sliders to match
		if (CloudSlider) CloudSlider->SetValue(P.Cloud);
		if (FogSlider) FogSlider->SetValue(P.Fog);
		if (RainSlider) RainSlider->SetValue(P.Rain);
		if (SnowSlider) SnowSlider->SetValue(P.Snow);
		if (ThunderSlider) ThunderSlider->SetValue(P.Thunder);
	}
}

void AWeatherDebugMenu::OnTimeSliderChanged(float Value)
{
	float TOD = Value * 2400.f;
	SetFloatProp(UDSActor, FName("Time Of Day"), TOD);
}

void AWeatherDebugMenu::OnCloudSliderChanged(float Value)
{
	SetFloatProp(UDSActor, FName("Cloud Coverage"), Value);
}

void AWeatherDebugMenu::OnFogSliderChanged(float Value)
{
	SetFloatProp(UDSActor, FName("Fog"), Value);
}

void AWeatherDebugMenu::OnRainSliderChanged(float Value)
{
	if (UDWActor)
	{
		SetFloatProp(UDWActor, FName("Rain"), Value);
		SetBoolProp(UDWActor, FName("Rain - Manual Override"), true);
	}
}

void AWeatherDebugMenu::OnSnowSliderChanged(float Value)
{
	if (UDWActor)
	{
		SetFloatProp(UDWActor, FName("Snow"), Value);
		SetBoolProp(UDWActor, FName("Snow - Manual Override"), true);
	}
}

void AWeatherDebugMenu::OnThunderSliderChanged(float Value)
{
	if (UDWActor)
	{
		SetFloatProp(UDWActor, FName("Thunder/ Lightning"), Value);
		SetBoolProp(UDWActor, FName("Thunder/ Lightning - Manual Override"), true);
	}
}

void AWeatherDebugMenu::OnDaySpeedSliderChanged(float Value)
{
	// 0..1 maps to 0.01..10 minutes
	float Minutes = FMath::Lerp(0.01f, 10.f, Value);
	SetFloatProp(UDSActor, FName("Day Length"), Minutes);
}

void AWeatherDebugMenu::OnNightSpeedSliderChanged(float Value)
{
	float Minutes = FMath::Lerp(0.01f, 10.f, Value);
	SetFloatProp(UDSActor, FName("Night Length"), Minutes);
}

void AWeatherDebugMenu::OnDayClicked()
{
	SetFloatProp(UDSActor, FName("Time Of Day"), 1200.f);
	if (TimeSlider) TimeSlider->SetValue(0.5f);
}

void AWeatherDebugMenu::OnNightClicked()
{
	SetFloatProp(UDSActor, FName("Time Of Day"), 2200.f);
	if (TimeSlider) TimeSlider->SetValue(2200.f / 2400.f);
}

void AWeatherDebugMenu::OnAnimateChanged(bool bIsChecked)
{
	SetBoolProp(UDSActor, FName("Animate Time of Day"), bIsChecked);
}
