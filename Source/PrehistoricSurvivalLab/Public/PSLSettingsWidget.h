#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSLUiStyle.h"
#include "PSLSettingsWidget.generated.h"

class UCheckBox;
class UPSLUiButton;
class USlider;
class UTextBlock;
class UWidgetSwitcher;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPSLSettingsClosedSignature);

/**
 * Tabs Graphics / Audio / Controls / Language plus Apply / Revert / Back. Changes preview live (audio, culture, graphics
 * go through UPSLGameUserSettings); they persist only on Apply. Back with unapplied changes asks for a second click.
 */
UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLSettingsWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    enum class ETab : uint8 { Graphics, Audio, Controls, Language };

    virtual bool Initialize() override;

    FPSLSettingsClosedSignature OnClosed;

    void SelectTab(ETab Tab);
    ETab GetActiveTab() const { return ActiveTab; }
    bool HasUnappliedChanges() const { return bDirty; }
    bool IsConfirmingDiscard() const { return bConfirmDiscard; }

    UPSLUiButton* GetTabButton(ETab Tab) const { return TabButtons[static_cast<int32>(Tab)]; }
    UPSLUiButton* GetApplyButton() const { return ApplyButton; }
    UPSLUiButton* GetRevertButton() const { return RevertButton; }
    UPSLUiButton* GetBackButton() const { return BackButton; }
    UPSLUiButton* GetQualityButton() const { return QualityButton; }
    UPSLUiButton* GetFrameCapButton() const { return FrameCapButton; }
    UPSLUiButton* GetWindowModeButton() const { return WindowModeButton; }
    UPSLUiButton* GetEnglishButton() const { return EnglishButton; }
    UPSLUiButton* GetVietnameseButton() const { return VietnameseButton; }
    USlider* GetResolutionScaleSlider() const { return ResolutionScaleSlider; }
    USlider* GetMasterSlider() const { return MasterSlider; }
    USlider* GetMusicSlider() const { return MusicSlider; }
    USlider* GetSfxSlider() const { return SfxSlider; }
    USlider* GetSensitivitySlider() const { return SensitivitySlider; }
    UCheckBox* GetVSyncCheck() const { return VSyncCheck; }
    UCheckBox* GetInvertYCheck() const { return InvertYCheck; }
    UTextBlock* GetStatusText() const { return StatusText; }

    // Re-reads every control from the saved settings (used on open and by Revert).
    void LoadFromSettings();

    // Persist (ApplySettings + SaveSettings). Returns false when no PSL settings object exists.
    bool Apply();
    // Drops unapplied changes and restores the controls.
    void Revert();
    // First call with unapplied changes only arms the confirmation; the next call (or no changes) reverts and closes.
    void RequestBack();

    // Rebuilds visible text for the current culture.
    void RefreshTexts();

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    // P / Escape = Back (with the discard confirmation when dirty).
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
    void BuildTree();
    void MarkDirty();
    void ApplyLivePreview();
    void UpdateValueTexts();
    void SetCulture(const FString& Culture);
    void HandleCultureChanged();

    UTextBlock* AddLabel(TFunction<FText()> Getter, int32 Size, const FLinearColor& Color, EPSLFontWeight Weight = EPSLFontWeight::Regular);
    UPSLUiButton* AddButton(TFunction<FText()> Getter);
    UFUNCTION() void OnTabGraphics();
    UFUNCTION() void OnTabAudio();
    UFUNCTION() void OnTabControls();
    UFUNCTION() void OnTabLanguage();
    UFUNCTION() void OnApply();
    UFUNCTION() void OnRevert();
    UFUNCTION() void OnBack();
    UFUNCTION() void OnQuality();
    UFUNCTION() void OnFrameCap();
    UFUNCTION() void OnWindowMode();
    UFUNCTION() void OnEnglish();
    UFUNCTION() void OnVietnamese();
    UFUNCTION() void OnSliderChanged(float Value);
    UFUNCTION() void OnCheckChanged(bool bChecked);

    ETab ActiveTab = ETab::Graphics;
    bool bDirty = false;
    bool bConfirmDiscard = false;
    bool bLoading = false;
    FDelegateHandle CultureHandle;

    // Pending graphics choices (applied only on Apply).
    int32 PendingQuality = 2;
    int32 PendingFrameCap = 1;
    int32 PendingWindowMode = 0;
    FString PendingCulture;

    // Live-previewed values as last loaded/applied; Revert restores these on the settings object.
    float SavedMaster = 1.0f;
    float SavedMusic = 0.8f;
    float SavedSfx = 1.0f;
    float SavedSensitivity = 1.0f;
    bool bSavedInvertY = false;
    FString SavedCulture;

    UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> Pages;
    UPROPERTY(Transient) TArray<TObjectPtr<UPSLUiButton>> TabButtons;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> ApplyButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> RevertButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> BackButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> QualityButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> FrameCapButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> WindowModeButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> EnglishButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> VietnameseButton;
    UPROPERTY(Transient) TObjectPtr<USlider> ResolutionScaleSlider;
    UPROPERTY(Transient) TObjectPtr<USlider> MasterSlider;
    UPROPERTY(Transient) TObjectPtr<USlider> MusicSlider;
    UPROPERTY(Transient) TObjectPtr<USlider> SfxSlider;
    UPROPERTY(Transient) TObjectPtr<USlider> SensitivitySlider;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> VSyncCheck;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> InvertYCheck;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ValueTexts;
    // Every localized label/button text is stored with its getter so RefreshTexts can re-resolve it on a culture change.
    TArray<TWeakObjectPtr<UTextBlock>> LabelWidgets;
    TArray<TFunction<FText()>> LabelGetters;
};
