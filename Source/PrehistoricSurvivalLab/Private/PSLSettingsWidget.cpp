#include "PSLSettingsWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Internationalization/Internationalization.h"
#include "PSLGameUserSettings.h"
#include "PSLUiButton.h"
#include "PSLUiStyle.h"

#define LOCTEXT_NAMESPACE "PSL"

namespace
{
constexpr int32 TabCount = 4;
constexpr int32 QualityCustom = 4; // 0..3 = Low..Epic; GetOverallScalabilityLevel() returns -1 for mixed levels.
enum EValueText : int32 { VT_ResolutionScale, VT_Master, VT_Music, VT_Sfx, VT_Sensitivity, VT_Count };

FText QualityName(int32 Quality)
{
    switch (Quality)
    {
    case 0: return LOCTEXT("Settings_QualityLow", "Low");
    case 1: return LOCTEXT("Settings_QualityMedium", "Medium");
    case 2: return LOCTEXT("Settings_QualityHigh", "High");
    case 3: return LOCTEXT("Settings_QualityEpic", "Epic");
    default: return LOCTEXT("Settings_QualityCustom", "Custom");
    }
}

FText FrameCapName(int32 Choice)
{
    switch (Choice)
    {
    case 0: return LOCTEXT("Settings_Fps30", "30 FPS");
    case 1: return LOCTEXT("Settings_Fps60", "60 FPS");
    case 2: return LOCTEXT("Settings_Fps120", "120 FPS");
    default: return LOCTEXT("Settings_FpsUnlimited", "Unlimited");
    }
}

FText WindowModeName(int32 Mode)
{
    switch (Mode)
    {
    case EWindowMode::Fullscreen: return LOCTEXT("Settings_WindowFullscreen", "Fullscreen");
    case EWindowMode::WindowedFullscreen: return LOCTEXT("Settings_WindowBorderless", "Borderless");
    default: return LOCTEXT("Settings_WindowWindowed", "Windowed");
    }
}

// Percent sliders show 0..100; the stored value is 0..1.
FText PercentText(float Value01)
{
    return FText::AsNumber(FMath::RoundToInt(Value01 * 100.0f));
}
}

bool UPSLSettingsWidget::Initialize()
{
    const bool bResult = Super::Initialize();
    if (bResult && WidgetTree && !WidgetTree->RootWidget)
    {
        BuildTree();
        LoadFromSettings();
    }
    return bResult;
}

UTextBlock* UPSLSettingsWidget::AddLabel(TFunction<FText()> Getter, int32 Size, const FLinearColor& Color, EPSLFontWeight Weight)
{
    UTextBlock* Text = FPSLUiStyle::MakeText(WidgetTree, Getter(), Size, Color, Weight);
    LabelWidgets.Add(Text);
    LabelGetters.Add(MoveTemp(Getter));
    return Text;
}

UPSLUiButton* UPSLSettingsWidget::AddButton(TFunction<FText()> Getter)
{
    UPSLUiButton* Button = UPSLUiButton::Make(WidgetTree, Getter());
    LabelWidgets.Add(Button->GetLabel());
    LabelGetters.Add(MoveTemp(Getter));
    return Button;
}

void UPSLSettingsWidget::BuildTree()
{
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
    WidgetTree->RootWidget = Root;

    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    Panel->SetBrushColor(FPSLUiStyle::Panel());
    Panel->SetPadding(FMargin(32.0f));
    UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
    PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
    PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
    PanelSlot->SetAutoSize(true);
    PanelSlot->SetPosition(FVector2D::ZeroVector);

    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    Panel->SetContent(Column);
    Column->AddChildToVerticalBox(AddLabel([] { return LOCTEXT("Settings_Title", "SETTINGS"); }, FPSLUiStyle::HeadingSize, FPSLUiStyle::Accent(), EPSLFontWeight::Bold))
        ->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));

    auto MakeRow = [this]() { return WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass()); };
    auto Fill = [](UHorizontalBoxSlot* Slot) { Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); Slot->SetPadding(FMargin(4.0f, 2.0f)); Slot->SetVerticalAlignment(VAlign_Center); };

    // Tabs.
    UHorizontalBox* Tabs = MakeRow();
    Column->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
    const TFunction<FText()> TabNames[TabCount] = {
        [] { return LOCTEXT("Settings_TabGraphics", "Graphics"); }, [] { return LOCTEXT("Settings_TabAudio", "Audio"); },
        [] { return LOCTEXT("Settings_TabControls", "Controls"); }, [] { return LOCTEXT("Settings_TabLanguage", "Language"); }};
    TabButtons.Reset();
    for (const TFunction<FText()>& Name : TabNames)
    {
        UPSLUiButton* Tab = AddButton(Name);
        Fill(Tabs->AddChildToHorizontalBox(Tab));
        TabButtons.Add(Tab);
    }
    TabButtons[0]->OnClicked.AddDynamic(this, &UPSLSettingsWidget::OnTabGraphics);
    TabButtons[1]->OnClicked.AddDynamic(this, &UPSLSettingsWidget::OnTabAudio);
    TabButtons[2]->OnClicked.AddDynamic(this, &UPSLSettingsWidget::OnTabControls);
    TabButtons[3]->OnClicked.AddDynamic(this, &UPSLSettingsWidget::OnTabLanguage);

    Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("Pages"));
    Column->AddChildToVerticalBox(Pages);
    auto AddPage = [this]()
    {
        UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
        Pages->AddChild(Page);
        return Page;
    };
    // One labelled row: caption on the left, Control in the middle, optional value text on the right.
    auto AddRow = [&](UVerticalBox* Page, TFunction<FText()> Caption, UWidget* Control, int32 ValueIndex)
    {
        UHorizontalBox* Row = MakeRow();
        Page->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.0f, 4.0f));
        Fill(Row->AddChildToHorizontalBox(AddLabel(MoveTemp(Caption), FPSLUiStyle::BodySize, FPSLUiStyle::Text())));
        Fill(Row->AddChildToHorizontalBox(Control));
        if (ValueIndex >= 0)
        {
            UTextBlock* Value = FPSLUiStyle::MakeText(WidgetTree, FText::GetEmpty(), FPSLUiStyle::BodySize, FPSLUiStyle::Accent());
            Row->AddChildToHorizontalBox(Value)->SetPadding(FMargin(8.0f, 2.0f));
            ValueTexts[ValueIndex] = Value;
        }
    };
    auto MakeSlider = [this](float Min, float Max, float Step)
    {
        USlider* Slider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
        Slider->SetMinValue(Min);
        Slider->SetMaxValue(Max);
        Slider->SetStepSize(Step);
        Slider->SetSliderHandleColor(FPSLUiStyle::Accent());
        Slider->OnValueChanged.AddDynamic(this, &UPSLSettingsWidget::OnSliderChanged);
        return Slider;
    };
    auto MakeCheck = [this]()
    {
        UCheckBox* Check = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
        Check->OnCheckStateChanged.AddDynamic(this, &UPSLSettingsWidget::OnCheckChanged);
        return Check;
    };
    ValueTexts.Init(nullptr, VT_Count);

    UVerticalBox* Graphics = AddPage();
    QualityButton = AddButton([this] { return QualityName(PendingQuality); });
    QualityButton->OnClicked.AddDynamic(this, &UPSLSettingsWidget::OnQuality);
    AddRow(Graphics, [] { return LOCTEXT("Settings_Quality", "Quality preset"); }, QualityButton, -1);
    ResolutionScaleSlider = MakeSlider(50.0f, 100.0f, 1.0f);
    AddRow(Graphics, [] { return LOCTEXT("Settings_ResolutionScale", "Resolution scale"); }, ResolutionScaleSlider, VT_ResolutionScale);
    VSyncCheck = MakeCheck();
    AddRow(Graphics, [] { return LOCTEXT("Settings_VSync", "VSync"); }, VSyncCheck, -1);
    FrameCapButton = AddButton([this] { return FrameCapName(PendingFrameCap); });
    FrameCapButton->OnClicked.AddDynamic(this, &UPSLSettingsWidget::OnFrameCap);
    AddRow(Graphics, [] { return LOCTEXT("Settings_FrameCap", "Frame rate cap"); }, FrameCapButton, -1);
    WindowModeButton = AddButton([this] { return WindowModeName(PendingWindowMode); });
    WindowModeButton->OnClicked.AddDynamic(this, &UPSLSettingsWidget::OnWindowMode);
    AddRow(Graphics, [] { return LOCTEXT("Settings_WindowMode", "Window mode"); }, WindowModeButton, -1);

    UVerticalBox* Audio = AddPage();
    MasterSlider = MakeSlider(0.0f, 100.0f, 1.0f);
    AddRow(Audio, [] { return LOCTEXT("Settings_Master", "Master volume"); }, MasterSlider, VT_Master);
    MusicSlider = MakeSlider(0.0f, 100.0f, 1.0f);
    AddRow(Audio, [] { return LOCTEXT("Settings_Music", "Music volume"); }, MusicSlider, VT_Music);
    SfxSlider = MakeSlider(0.0f, 100.0f, 1.0f);
    AddRow(Audio, [] { return LOCTEXT("Settings_Sfx", "Effects volume"); }, SfxSlider, VT_Sfx);

    UVerticalBox* Controls = AddPage();
    SensitivitySlider = MakeSlider(UPSLGameUserSettings::MinSensitivity, UPSLGameUserSettings::MaxSensitivity, 0.05f);
    AddRow(Controls, [] { return LOCTEXT("Settings_Sensitivity", "Mouse sensitivity"); }, SensitivitySlider, VT_Sensitivity);
    InvertYCheck = MakeCheck();
    AddRow(Controls, [] { return LOCTEXT("Settings_InvertY", "Invert Y axis"); }, InvertYCheck, -1);

    UVerticalBox* Language = AddPage();
    EnglishButton = AddButton([] { return LOCTEXT("Settings_English", "English"); });
    EnglishButton->OnClicked.AddDynamic(this, &UPSLSettingsWidget::OnEnglish);
    Language->AddChildToVerticalBox(EnglishButton)->SetPadding(FMargin(0.0f, 2.0f));
    // Endonym on purpose: a language is always listed in its own name.
    VietnameseButton = AddButton([] { return LOCTEXT("Settings_Vietnamese", "Tiếng Việt"); });
    VietnameseButton->OnClicked.AddDynamic(this, &UPSLSettingsWidget::OnVietnamese);
    Language->AddChildToVerticalBox(VietnameseButton)->SetPadding(FMargin(0.0f, 2.0f));

    StatusText = FPSLUiStyle::MakeText(WidgetTree, FText::GetEmpty(), FPSLUiStyle::BodySize, FPSLUiStyle::Danger());
    // Status/confirm texts (vi ~66 chars) wrap at 560 px instead of stretching the panel.
    StatusText->SetAutoWrapText(true);
    StatusText->SetWrapTextAt(560.0f);
    Column->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 4.0f));

    UHorizontalBox* Footer = MakeRow();
    Column->AddChildToVerticalBox(Footer);
    ApplyButton = AddButton([] { return LOCTEXT("Settings_Apply", "Apply"); });
    RevertButton = AddButton([] { return LOCTEXT("Settings_Revert", "Revert"); });
    BackButton = AddButton([] { return LOCTEXT("Settings_Back", "Back"); });
    for (UPSLUiButton* Button : {ApplyButton.Get(), RevertButton.Get(), BackButton.Get()})
    {
        Fill(Footer->AddChildToHorizontalBox(Button));
    }
    ApplyButton->OnClicked.AddDynamic(this, &UPSLSettingsWidget::OnApply);
    RevertButton->OnClicked.AddDynamic(this, &UPSLSettingsWidget::OnRevert);
    BackButton->OnClicked.AddDynamic(this, &UPSLSettingsWidget::OnBack);

    SelectTab(ETab::Graphics);
}

void UPSLSettingsWidget::NativeConstruct()
{
    Super::NativeConstruct();
    SetIsFocusable(true);
    if (!CultureHandle.IsValid())
    {
        CultureHandle = FInternationalization::Get().OnCultureChanged().AddUObject(this, &UPSLSettingsWidget::HandleCultureChanged);
    }
    RefreshTexts();
}

void UPSLSettingsWidget::NativeDestruct()
{
    if (CultureHandle.IsValid())
    {
        FInternationalization::Get().OnCultureChanged().Remove(CultureHandle);
        CultureHandle.Reset();
    }
    Super::NativeDestruct();
}

void UPSLSettingsWidget::SelectTab(ETab Tab)
{
    ActiveTab = Tab;
    Pages->SetActiveWidgetIndex(static_cast<int32>(Tab));
    for (int32 Index = 0; Index < TabButtons.Num(); ++Index)
    {
        TabButtons[Index]->GetLabel()->SetColorAndOpacity(FSlateColor(Index == static_cast<int32>(Tab) ? FPSLUiStyle::Accent() : FPSLUiStyle::Text()));
    }
}

void UPSLSettingsWidget::LoadFromSettings()
{
    const UPSLGameUserSettings* Settings = UPSLGameUserSettings::Get();
    if (!Settings)
    {
        StatusText->SetText(LOCTEXT("Settings_Unavailable", "Settings are unavailable."));
        return;
    }
    // Slider/check setters broadcast their change events; bLoading keeps those from marking the screen dirty.
    TGuardValue<bool> Loading(bLoading, true);
    const int32 Overall = Settings->GetOverallScalabilityLevel();
    PendingQuality = Overall >= 0 && Overall < QualityCustom ? Overall : QualityCustom;
    PendingFrameCap = Settings->GetFrameRateChoice();
    PendingWindowMode = Settings->GetFullscreenMode();
    PendingCulture = UPSLGameUserSettings::ResolveCultureName(Settings->CultureName);
    float Normalized, Scale, MinScale, MaxScale;
    Settings->GetResolutionScaleInformationEx(Normalized, Scale, MinScale, MaxScale);
    // 0 is the engine default (display-based screen percentage, not 100%); the slider shows 100 for it.
    LoadedResolutionScale = Scale;
    LoadedResolutionSlider = Scale > 0.0f ? FMath::Clamp(Scale, 50.0f, 100.0f) : 100.0f;
    ResolutionScaleSlider->SetValue(LoadedResolutionSlider);
    VSyncCheck->SetIsChecked(Settings->IsVSyncEnabled());
    MasterSlider->SetValue(Settings->MasterVolume * 100.0f);
    MusicSlider->SetValue(Settings->MusicVolume * 100.0f);
    SfxSlider->SetValue(Settings->SfxVolume * 100.0f);
    SensitivitySlider->SetValue(Settings->MouseSensitivity);
    InvertYCheck->SetIsChecked(Settings->bInvertY);

    SavedMaster = Settings->MasterVolume;
    SavedMusic = Settings->MusicVolume;
    SavedSfx = Settings->SfxVolume;
    SavedSensitivity = Settings->MouseSensitivity;
    bSavedInvertY = Settings->bInvertY;
    SavedCulture = Settings->CultureName;

    bDirty = false;
    bConfirmDiscard = false;
    StatusText->SetText(FText::GetEmpty());
    RefreshTexts();
}

void UPSLSettingsWidget::MarkDirty()
{
    if (bLoading)
    {
        return;
    }
    bDirty = true;
    bConfirmDiscard = false;
    StatusText->SetText(FText::GetEmpty());
    ApplyLivePreview();
    RefreshTexts();
}

void UPSLSettingsWidget::ApplyLivePreview()
{
    UPSLGameUserSettings* Settings = UPSLGameUserSettings::Get();
    if (!Settings)
    {
        return;
    }
    // Audio and look settings are cheap and safe to preview; graphics wait for Apply (a mode change can resize the window).
    Settings->SetMasterVolume(MasterSlider->GetValue() / 100.0f);
    Settings->SetMusicVolume(MusicSlider->GetValue() / 100.0f);
    Settings->SetSfxVolume(SfxSlider->GetValue() / 100.0f);
    Settings->SetMouseSensitivity(SensitivitySlider->GetValue());
    Settings->bInvertY = InvertYCheck->IsChecked();
    Settings->ApplyAudio(GetWorld());
}

bool UPSLSettingsWidget::Apply()
{
    UPSLGameUserSettings* Settings = UPSLGameUserSettings::Get();
    if (!Settings)
    {
        StatusText->SetText(LOCTEXT("Settings_Unavailable", "Settings are unavailable."));
        return false;
    }
    ApplyLivePreview();
    if (PendingQuality < QualityCustom)
    {
        Settings->SetOverallScalabilityLevel(PendingQuality);
    }
    // After the preset: a moved slider is the player's explicit choice and wins over the preset's screen percentage; an untouched slider restores the loaded value, so 0 keeps the engine's display-based default instead of forcing 100% (47 ms/frame at 2560x1440 in packaged QA).
    const float ScaleChoice = ResolutionScaleSlider->GetValue();
    Settings->SetResolutionScaleValueEx(FMath::IsNearlyEqual(ScaleChoice, LoadedResolutionSlider) ? LoadedResolutionScale : ScaleChoice);
    Settings->SetVSyncEnabled(VSyncCheck->IsChecked());
    Settings->SetFrameRateChoice(PendingFrameCap);
    Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(PendingWindowMode));
    Settings->CultureName = PendingCulture;
    // UPSLGameUserSettings::ApplySettings applies graphics, culture and audio; the base class saves (GameUserSettings.cpp:600).
    Settings->ApplySettings(false);
    LoadFromSettings();
    StatusText->SetText(LOCTEXT("Settings_Applied", "Settings applied."));
    return true;
}

void UPSLSettingsWidget::Revert()
{
    if (UPSLGameUserSettings* Settings = UPSLGameUserSettings::Get())
    {
        Settings->SetMasterVolume(SavedMaster);
        Settings->SetMusicVolume(SavedMusic);
        Settings->SetSfxVolume(SavedSfx);
        Settings->SetMouseSensitivity(SavedSensitivity);
        Settings->bInvertY = bSavedInvertY;
        Settings->CultureName = SavedCulture;
        Settings->ApplyAudio(GetWorld());
        Settings->ApplyCulture();
    }
    LoadFromSettings();
}

void UPSLSettingsWidget::RequestBack()
{
    if (bDirty && !bConfirmDiscard)
    {
        bConfirmDiscard = true;
        StatusText->SetText(LOCTEXT("Settings_ConfirmDiscard", "You have unapplied changes. Press Back again to discard them."));
        return;
    }
    if (bDirty)
    {
        Revert();
    }
    bConfirmDiscard = false;
    OnClosed.Broadcast();
}

void UPSLSettingsWidget::UpdateValueTexts()
{
    if (ValueTexts.Num() != VT_Count)
    {
        return;
    }
    ValueTexts[VT_ResolutionScale]->SetText(FText::Format(LOCTEXT("Settings_PercentValue", "{0}%"), FText::AsNumber(FMath::RoundToInt(ResolutionScaleSlider->GetValue()))));
    ValueTexts[VT_Master]->SetText(PercentText(MasterSlider->GetValue() / 100.0f));
    ValueTexts[VT_Music]->SetText(PercentText(MusicSlider->GetValue() / 100.0f));
    ValueTexts[VT_Sfx]->SetText(PercentText(SfxSlider->GetValue() / 100.0f));
    FNumberFormattingOptions TwoDecimals;
    TwoDecimals.SetMinimumFractionalDigits(2).SetMaximumFractionalDigits(2);
    ValueTexts[VT_Sensitivity]->SetText(FText::AsNumber(SensitivitySlider->GetValue(), &TwoDecimals));
}

void UPSLSettingsWidget::RefreshTexts()
{
    for (int32 Index = 0; Index < LabelWidgets.Num(); ++Index)
    {
        if (UTextBlock* Label = LabelWidgets[Index].Get())
        {
            Label->SetText(LabelGetters[Index]());
        }
    }
    UpdateValueTexts();
    EnglishButton->GetLabel()->SetColorAndOpacity(FSlateColor(PendingCulture == TEXT("en") ? FPSLUiStyle::Accent() : FPSLUiStyle::Text()));
    VietnameseButton->GetLabel()->SetColorAndOpacity(FSlateColor(PendingCulture == TEXT("vi") ? FPSLUiStyle::Accent() : FPSLUiStyle::Text()));
}

void UPSLSettingsWidget::SetCulture(const FString& Culture)
{
    if (PendingCulture == Culture)
    {
        return;
    }
    PendingCulture = Culture;
    // Live preview without touching config; Apply persists it, Revert/Back restores the saved culture.
    FInternationalization::Get().SetCurrentCulture(Culture);
    MarkDirty();
}

void UPSLSettingsWidget::HandleCultureChanged()
{
    RefreshTexts();
}

FReply UPSLSettingsWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::P)
    {
        RequestBack();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UPSLSettingsWidget::OnTabGraphics() { SelectTab(ETab::Graphics); }
void UPSLSettingsWidget::OnTabAudio() { SelectTab(ETab::Audio); }
void UPSLSettingsWidget::OnTabControls() { SelectTab(ETab::Controls); }
void UPSLSettingsWidget::OnTabLanguage() { SelectTab(ETab::Language); }

void UPSLSettingsWidget::OnApply() { Apply(); }
void UPSLSettingsWidget::OnRevert() { Revert(); }
void UPSLSettingsWidget::OnBack() { RequestBack(); }

void UPSLSettingsWidget::OnQuality()
{
    // Cycles Low -> Medium -> High -> Epic; Custom (mixed levels) is shown, never picked.
    PendingQuality = PendingQuality >= 3 ? 0 : PendingQuality + 1;
    MarkDirty();
}

void UPSLSettingsWidget::OnFrameCap()
{
    PendingFrameCap = (PendingFrameCap + 1) % 4;
    MarkDirty();
}

void UPSLSettingsWidget::OnWindowMode()
{
    // Windowed -> Fullscreen -> Borderless -> Windowed.
    switch (PendingWindowMode)
    {
    case EWindowMode::Windowed: PendingWindowMode = EWindowMode::Fullscreen; break;
    case EWindowMode::Fullscreen: PendingWindowMode = EWindowMode::WindowedFullscreen; break;
    default: PendingWindowMode = EWindowMode::Windowed; break;
    }
    MarkDirty();
}

void UPSLSettingsWidget::OnEnglish() { SetCulture(TEXT("en")); }
void UPSLSettingsWidget::OnVietnamese() { SetCulture(TEXT("vi")); }

void UPSLSettingsWidget::OnSliderChanged(float Value)
{
    MarkDirty();
}

void UPSLSettingsWidget::OnCheckChanged(bool bChecked)
{
    MarkDirty();
}

#undef LOCTEXT_NAMESPACE
