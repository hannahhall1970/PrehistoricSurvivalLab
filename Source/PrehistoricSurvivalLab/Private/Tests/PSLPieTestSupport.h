#pragma once

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Editor/EditorEngine.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "GameFramework/InputDeviceLibrary.h"
#include "InputKeyEventArgs.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PSLCharacter.h"
#include "PSLGameMode.h"
#include "PSLHUD.h"
#include "PSLSurvivalComponent.h"
#include "PSLDinoAnimInstance.h"
#include "PSLPreyAIController.h"
#include "PSLPreyCharacter.h"
#include "PSLPredatorAIController.h"
#include "PSLPredatorCharacter.h"
#include "PSLStatsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "UnrealClient.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "Widgets/SWindow.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "Framework/Application/SlateApplication.h"
#include "Camera/CameraActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "UObject/UObjectIterator.h"
#include "LandscapeProxy.h"

// Shared PIE test helpers (moved verbatim from PSLPieTests.cpp; static -> inline).
namespace PSLPie
{
constexpr const TCHAR* ArenaMapPath = TEXT("/Game/Maps/L_TestArena");
constexpr float FrameDelta = 1.0f / 60.0f;

// Weak object reference that keeps the raw-pointer call syntax (`Ctx.World->X`, `*Ctx.Character`, implicit T*).
// A PIE world torn down mid-test makes Get() null (HasObjects() false) instead of leaving a dangling pointer.
template <typename T>
struct TPSLWeakRef
{
    T* Get() const { return Ptr.Get(); }
    bool IsValid() const { return Ptr.IsValid(); }
    operator T*() const { return Ptr.Get(); }
    T* operator->() const { return Ptr.Get(); }
    T& operator*() const { return *Ptr.Get(); }
    TPSLWeakRef& operator=(T* InPtr) { Ptr = InPtr; return *this; }

private:
    TWeakObjectPtr<T> Ptr;
};

struct FPSLPieCtx
{
    explicit FPSLPieCtx(FAutomationTestBase* InTest, const TCHAR* InName)
        : Test(InTest)
        , TestName(InName)
    {
    }

    bool HasObjects() const
    {
        return World.IsValid() && GameViewport.IsValid() && PC.IsValid() && Character.IsValid();
    }

    bool IsUsable() const
    {
        return !bInvalid && HasObjects() && World->WorldType == EWorldType::PIE;
    }

    void MarkInvalid(const FString& Message)
    {
        if (!bInvalid)
        {
            bInvalid = true;
            if (Test)
            {
                Test->AddError(Message);
            }
        }
    }

    FAutomationTestBase* Test = nullptr;
    FString TestName;
    TPSLWeakRef<UWorld> World;
    TPSLWeakRef<UGameViewportClient> GameViewport;
    TPSLWeakRef<APlayerController> PC;
    TPSLWeakRef<APSLCharacter> Character;
    // When set, WaitForPlayer only accepts a PIE world whose map name contains this (used after OpenLevel).
    FString RequiredMap;
    bool bReady = false;
    double ReadyWorldTime = 0.0;
    bool bInvalid = false;
    TArray<FString> Measurements;
    TArray<FString> InputResults;
    FString LastInputPath = TEXT("none");
    bool bLastInputKeyReturn = false;
    bool bLastInputAxisReturn = false;
};

class FPSLPieStep final : public IAutomationLatentCommand
{
public:
    FPSLPieStep(TSharedRef<FPSLPieCtx> InContext, const TCHAR* InName, float InTimeout, TFunction<bool(FPSLPieCtx&)> InFunction)
        : Context(MoveTemp(InContext))
        , Name(InName)
        , Timeout(InTimeout)
        , Function(MoveTemp(InFunction))
    {
    }

    virtual bool Update() override
    {
        if (Context->bReady && !Context->HasObjects() && !Context->bInvalid)
        {
            Context->MarkInvalid(FString::Printf(TEXT("PIE context was lost during step %s"), *Name));
            return true;
        }

        if (Function(*Context))
        {
            return true;
        }

        if (GetCurrentRunTime() >= Timeout)
        {
            Context->MarkInvalid(FString::Printf(TEXT("PIE step %s timed out after %.1f seconds"), *Name, Timeout));
            return true;
        }
        return false;
    }

private:
    TSharedRef<FPSLPieCtx> Context;
    FString Name;
    float Timeout;
    TFunction<bool(FPSLPieCtx&)> Function;
};

inline void AddPieStep(const TSharedRef<FPSLPieCtx>& Context, const TCHAR* Name, float Timeout, TFunction<bool(FPSLPieCtx&)> Function)
{
    ADD_LATENT_AUTOMATION_COMMAND(FPSLPieStep(Context, Name, Timeout, MoveTemp(Function)));
}

inline FString JsonEscape(const FString& Value)
{
    FString Result = Value;
    Result.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Result.ReplaceInline(TEXT("\""), TEXT("\\\""));
    return Result;
}

inline const TCHAR* JsonBool(const bool Value)
{
    return Value ? TEXT("true") : TEXT("false");
}

inline const TCHAR* InputEventName(const EInputEvent Event)
{
    return Event == IE_Pressed ? TEXT("pressed") : TEXT("released");
}

inline float ActionMagnitude(const FPSLPieCtx& Context, const UInputAction* Action)
{
    if (!Action || !Context.PC)
    {
        return 0.0f;
    }

    if (const UEnhancedPlayerInput* EnhancedInput = Cast<UEnhancedPlayerInput>(Context.PC->PlayerInput))
    {
        return EnhancedInput->GetActionValue(Action).GetMagnitude();
    }
    return 0.0f;
}

inline void LogKeyInput(FPSLPieCtx& Context, const FKey& Key, EInputEvent Event, const FString& Path, bool bViewportReturn, bool bInputKeyReturn)
{
    Context.LastInputPath = Path;
    Context.bLastInputKeyReturn = bInputKeyReturn;
    Context.bLastInputAxisReturn = false;
    Context.InputResults.Add(FString::Printf(
        TEXT("{\"kind\":\"key\",\"key\":\"%s\",\"event\":\"%s\",\"path\":\"%s\",\"viewport_return\":%s,\"input_key_return\":%s}"),
        *JsonEscape(Key.ToString()), InputEventName(Event), *JsonEscape(Path), JsonBool(bViewportReturn), JsonBool(bInputKeyReturn)));
}

inline void LogAxisInput(FPSLPieCtx& Context, const FKey& Key, const FString& Path, bool bAxisReturn, bool bInputKeyReturn)
{
    Context.LastInputPath = Path;
    Context.bLastInputAxisReturn = bAxisReturn;
    Context.bLastInputKeyReturn = bInputKeyReturn;
    Context.InputResults.Add(FString::Printf(
        TEXT("{\"kind\":\"axis\",\"key\":\"%s\",\"path\":\"%s\",\"input_axis_return\":%s,\"input_key_return\":%s}"),
        *JsonEscape(Key.ToString()), *JsonEscape(Path), JsonBool(bAxisReturn), JsonBool(bInputKeyReturn)));
}

// GameViewportClient::InputKey resolves the LocalPlayer from the device id, so INPUTDEVICEID_NONE never reaches the PC.
inline FInputDeviceId PrimaryInputDevice(const FPSLPieCtx& Context)
{
    return UInputDeviceLibrary::GetPrimaryInputDeviceForUser(Context.PC->GetPlatformUserId());
}

inline bool SendKeyToPlayerController(FPSLPieCtx& Context, const FKey& Key, EInputEvent Event)
{
    if (!Context.IsUsable())
    {
        return false;
    }

    const FInputKeyEventArgs Args = FInputKeyEventArgs::CreateSimulated(
        Key, Event, 1.0f, -1, PrimaryInputDevice(Context), false, Context.GameViewport->Viewport);
    const bool bReturn = Context.PC->InputKey(Args);
    LogKeyInput(Context, Key, Event, TEXT("PlayerController.InputKey(fallback)"), false, bReturn);
    return bReturn;
}

inline bool SendKey(FPSLPieCtx& Context, const FKey& Key, EInputEvent Event)
{
    if (!Context.IsUsable())
    {
        return false;
    }

    const FInputKeyEventArgs Args = FInputKeyEventArgs::CreateSimulated(
        Key, Event, 1.0f, -1, PrimaryInputDevice(Context), false, Context.GameViewport->Viewport);
    const bool bViewportReturn = Context.GameViewport->InputKey(Args);
    if (bViewportReturn)
    {
        LogKeyInput(Context, Key, Event, TEXT("GameViewport.InputKey"), true, bViewportReturn);
        return bViewportReturn;
    }

    const bool bInputKeyReturn = Context.PC->InputKey(Args);
    LogKeyInput(Context, Key, Event, TEXT("PlayerController.InputKey(fallback)"), false, bInputKeyReturn);
    return bInputKeyReturn;
}

inline bool SendAxis(FPSLPieCtx& Context, const FKey& Key, float Delta, bool bForcePlayerController)
{
    if (!Context.IsUsable())
    {
        return false;
    }

    const FInputKeyEventArgs Args(
        Context.GameViewport->Viewport, PrimaryInputDevice(Context), Key, Delta, FrameDelta, 1, FPlatformTime::Cycles64());
    if (!bForcePlayerController)
    {
        const bool bAxisReturn = Context.GameViewport->InputAxis(Args);
        if (bAxisReturn)
        {
            LogAxisInput(Context, Key, TEXT("GameViewport.InputAxis"), bAxisReturn, false);
            return bAxisReturn;
        }
    }

    const bool bInputKeyReturn = Context.PC->InputKey(Args);
    LogAxisInput(Context, Key, TEXT("PlayerController.InputKey(axis fallback)"), false, bInputKeyReturn);
    return bInputKeyReturn;
}

inline void Record(FPSLPieCtx& Context, const TCHAR* Step)
{
    if (!Context.HasObjects())
    {
        return;
    }

    const UCharacterMovementComponent* Movement = Context.Character->GetCharacterMovement();
    const UPSLSurvivalComponent* Survival = Context.Character->GetSurvivalComponent();
    const FVector Location = Context.Character->GetActorLocation();
    const FVector Velocity = Context.Character->GetVelocity();
    const FRotator Rotation = Context.PC->GetControlRotation();
    const FString Inputs = Context.InputResults.Num() > 0 ? FString::Join(Context.InputResults, TEXT(",")) : FString();
    const FString Line = FString::Printf(
        TEXT("{\"step\":\"%s\",\"world_time\":%.6f,\"location\":[%.3f,%.3f,%.3f],\"velocity\":[%.3f,%.3f,%.3f],\"max_walk_speed\":%.3f,\"stamina\":%.3f,\"is_sprinting\":%s,\"is_sprint_locked\":%s,\"control_rotation\":[%.3f,%.3f,%.3f],\"injection_path\":\"%s\",\"input_key_return\":%s,\"input_axis_return\":%s,\"input_returns\":[%s]}"),
        Step,
        static_cast<double>(Context.World->GetTimeSeconds()),
        static_cast<double>(Location.X), static_cast<double>(Location.Y), static_cast<double>(Location.Z),
        static_cast<double>(Velocity.X), static_cast<double>(Velocity.Y), static_cast<double>(Velocity.Z),
        Movement ? static_cast<double>(Movement->MaxWalkSpeed) : 0.0,
        Survival ? static_cast<double>(Survival->GetStamina()) : 0.0,
        JsonBool(Survival && Survival->IsSprinting()), JsonBool(Survival && Survival->IsSprintLocked()),
        static_cast<double>(Rotation.Pitch), static_cast<double>(Rotation.Yaw), static_cast<double>(Rotation.Roll),
        *JsonEscape(Context.LastInputPath), JsonBool(Context.bLastInputKeyReturn), JsonBool(Context.bLastInputAxisReturn), *Inputs);
    Context.Measurements.Add(Line);
    if (Context.Test)
    {
        Context.Test->AddInfo(Line);
    }
}

// Frame-time distribution of one perf phase (milliseconds per frame).
struct FPSLFrameStats
{
    int32 Frames = 0;
    double AvgFps = 0.0;
    double AvgMs = 0.0;
    double P50Ms = 0.0;
    double P95Ms = 0.0;
    double P99Ms = 0.0;
    double MaxMs = 0.0;
    int32 Over33 = 0;
    int32 Over50 = 0;
};

// Nearest-rank percentile: index = ceil(Percent * N / 100) - 1 clamped to [0, N-1]; integer math avoids 0.95 * 100 rounding.
inline double PercentileMs(const TArray<double>& Sorted, int32 Percent)
{
    if (Sorted.IsEmpty())
    {
        return 0.0;
    }
    const int32 Count = Sorted.Num();
    return Sorted[FMath::Clamp((Percent * Count + 99) / 100 - 1, 0, Count - 1)];
}

inline FPSLFrameStats ComputeFrameStats(const TArray<double>& FrameMs)
{
    FPSLFrameStats Stats;
    Stats.Frames = FrameMs.Num();
    if (FrameMs.IsEmpty())
    {
        return Stats;
    }
    TArray<double> Sorted = FrameMs;
    Sorted.Sort();
    double Sum = 0.0;
    for (const double Ms : Sorted)
    {
        Sum += Ms;
        Stats.Over33 += Ms > 33.0 ? 1 : 0;
        Stats.Over50 += Ms > 50.0 ? 1 : 0;
    }
    Stats.AvgMs = Sum / Sorted.Num();
    Stats.AvgFps = Stats.AvgMs > 0.0 ? 1000.0 / Stats.AvgMs : 0.0;
    Stats.P50Ms = PercentileMs(Sorted, 50);
    Stats.P95Ms = PercentileMs(Sorted, 95);
    Stats.P99Ms = PercentileMs(Sorted, 99);
    Stats.MaxMs = Sorted.Last();
    return Stats;
}

// JSON members (no braces) so callers can embed them in an object or at top level.
inline FString FrameStatsJsonFields(const FPSLFrameStats& Stats)
{
    return FString::Printf(
        TEXT("\"frames\":%d,\"avgFps\":%.3f,\"avgMs\":%.3f,\"p50Ms\":%.3f,\"p95Ms\":%.3f,\"p99Ms\":%.3f,\"maxMs\":%.3f,\"over33ms\":%d,\"over50ms\":%d"),
        Stats.Frames, Stats.AvgFps, Stats.AvgMs, Stats.P50Ms, Stats.P95Ms, Stats.P99Ms, Stats.MaxMs, Stats.Over33, Stats.Over50);
}

inline void RequestScreenshot(FPSLPieCtx& Context, const TCHAR* Step)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("PIE"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = FPaths::Combine(Directory, Context.TestName + TEXT("_") + Step + TEXT(".png"));
    if (!FApp::CanEverRender())
    {
        if (Context.Test)
        {
            Context.Test->AddInfo(FString::Printf(TEXT("Screenshot skipped for %s: FApp::CanEverRender() is false"), *Filename));
        }
        return;
    }

    FScreenshotRequest::RequestScreenshot(Filename, true, false, false, FIntRect(), true);
    if (Context.Test)
    {
        Context.Test->AddInfo(FString::Printf(TEXT("Screenshot requested: %s"), *Filename));
    }
}

inline void SaveMeasurements(FPSLPieCtx& Context)
{
    if (Context.IsUsable())
    {
        Record(Context, TEXT("test_end"));
    }

    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("PIE"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = FPaths::Combine(Directory, Context.TestName + TEXT(".json"));
    FString Json = TEXT("[");
    for (int32 Index = 0; Index < Context.Measurements.Num(); ++Index)
    {
        if (Index > 0)
        {
            Json += TEXT(",");
        }
        Json += Context.Measurements[Index];
    }
    Json += TEXT("]");

    const bool bSaved = FFileHelper::SaveStringToFile(Json, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (Context.Test)
    {
        Context.Test->TestTrue(FString::Printf(TEXT("Measurement log saved: %s"), *Filename), bSaved);
    }
}

inline bool WaitForPlayer(FPSLPieCtx& Context)
{
    if (Context.bReady)
    {
        return Context.IsUsable() && Context.World->GetTimeSeconds() - Context.ReadyWorldTime >= 0.5;
    }

    if (!GEditor)
    {
        return false;
    }

    for (const FWorldContext& WorldContext : GEditor->GetWorldContexts())
    {
        if (WorldContext.WorldType != EWorldType::PIE || !WorldContext.GameViewport)
        {
            continue;
        }

        UWorld* World = WorldContext.World();
        if (World && !Context.RequiredMap.IsEmpty() && !World->GetMapName().Contains(Context.RequiredMap))
        {
            continue;
        }
        UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
        APlayerController* PC = GameInstance ? GameInstance->GetFirstLocalPlayerController(World) : nullptr;
        APSLCharacter* Character = PC ? Cast<APSLCharacter>(PC->GetPawn()) : nullptr;
        if (World && PC && Character)
        {
            Context.World = World;
            Context.GameViewport = WorldContext.GameViewport;
            Context.PC = PC;
            Context.Character = Character;
            Context.bReady = true;
            Context.ReadyWorldTime = World->GetTimeSeconds();
            return false;
        }
    }
    return false;
}

inline bool QueuePieStart(FAutomationTestBase& Test, const TSharedRef<FPSLPieCtx>& Context, const TCHAR* Map = ArenaMapPath)
{
    // In the editor, AutomationOpenMap already queues FWaitForEditorNavMeshBuildCommand and
    // FStartPIEForAutomationCommand (UEditorEngine::AutomationLoadMap). Queuing a second start restarts PIE.
    if (!Test.TestTrue(TEXT("AutomationOpenMap returned true"), AutomationOpenMap(Map)))
    {
        return false;
    }

    AddPieStep(Context, TEXT("WaitForPIEPlayer"), 30.0f, [](FPSLPieCtx& Ctx)
    {
        return WaitForPlayer(Ctx);
    });
    return true;
}

inline void QueuePieEnd(const TSharedRef<FPSLPieCtx>& Context)
{
    // A screenshot requested in the previous step is written during a later viewport draw; let PIE render first.
    AddPieStep(Context, TEXT("ScreenshotSettle"), 2.0f, [Frames = 0](FPSLPieCtx& Ctx) mutable
    {
        return !Ctx.IsUsable() || ++Frames >= 10;
    });
    AddPieStep(Context, TEXT("SaveMeasurements"), 2.0f, [](FPSLPieCtx& Ctx)
    {
        SaveMeasurements(Ctx);
        return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
}

// RequestScreenshot is served on a later viewport draw (UGameViewportClient::ProcessScreenShots); keep the
// current input held until that draw has happened, then record the state the capture shows.
inline void QueueAwaitScreenshot(const TSharedRef<FPSLPieCtx>& Context, const TCHAR* Label)
{
    AddPieStep(Context, TEXT("AwaitScreenshot"), 2.0f, [Label](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !FApp::CanEverRender())
        {
            return true;
        }
        if (FScreenshotRequest::IsScreenshotRequested())
        {
            return false;
        }
        Record(Ctx, Label);
        return true;
    });
}

inline void QueueReleaseSprint(const TSharedRef<FPSLPieCtx>& Context, const TCHAR* StillSprintingWhat)
{
    AddPieStep(Context, TEXT("ReleaseSprint"), 1.0f, [StillSprintingWhat](FPSLPieCtx& Ctx)
    {
        if (Ctx.IsUsable())
        {
            const UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
            Ctx.Test->TestTrue(StillSprintingWhat, Survival && Survival->IsSprinting());
            SendKey(Ctx, EKeys::LeftShift, IE_Released);
            SendKey(Ctx, EKeys::W, IE_Released);
        }
        return true;
    });
}

inline bool EnsureHeldAction(FPSLPieCtx& Context, const FKey& Key, UInputAction* Action, EInputEvent Event, bool& bFallbackSent)
{
    if (ActionMagnitude(Context, Action) > KINDA_SMALL_NUMBER)
    {
        return true;
    }
    if (!bFallbackSent)
    {
        SendKeyToPlayerController(Context, Key, Event);
        bFallbackSent = true;
    }
    return ActionMagnitude(Context, Action) > KINDA_SMALL_NUMBER;
}

inline EPSLPreyState PreyStateOf(const APSLPreyCharacter& Prey)
{
    const APSLPreyAIController* AI = Cast<APSLPreyAIController>(Prey.GetController());
    return AI ? AI->GetPreyState() : EPSLPreyState::Idle;
}

// Keeps the prey in place for bite tests (the AI re-issues flee moves every 0.2 s) and re-aims the player at it.
inline void HoldPreyStill(FPSLPieCtx& Ctx, APSLPreyCharacter& Prey)
{
    if (AController* AI = Prey.GetController())
    {
        AI->StopMovement();
    }
    Prey.GetCharacterMovement()->StopMovementImmediately();
    const FRotator ToPrey = (Prey.GetActorLocation() - Ctx.Character->GetActorLocation()).GetSafeNormal2D().Rotation();
    Ctx.PC->SetControlRotation(FRotator(0.0, ToPrey.Yaw, 0.0));
    Ctx.Character->SetActorRotation(FRotator(0.0, ToPrey.Yaw, 0.0));
}

inline EPSLDinoAction PlayerActiveAction(const FPSLPieCtx& Ctx)
{
    const UPSLDinoAnimInstance* Anim = Cast<UPSLDinoAnimInstance>(Ctx.Character->GetMesh()->GetAnimInstance());
    return Anim ? Anim->GetActiveAction() : EPSLDinoAction::None;
}

// Shared between the steps of a prey-death test: standing height, when the prey died and where its capsule was.
struct FPSLPreyDeathPose
{
    float AliveHeight = 0.0f;
    double DeathTime = 0.0;
    FVector DeathLocation = FVector::ZeroVector;
};

// ACharacter meshes default to AlwaysTickPose, which does not refresh bones while unrendered (-nullrhi gate), so the
// height below would stay at the reference pose. Test-only: gameplay code is unchanged.
inline void TrackPreyBones(APSLPreyCharacter& Prey)
{
    Prey.GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

// Z of bone Hips above the capsule bottom: ~50 uu standing, ~20 uu lying (the mesh is scaled to the capsule, not the raw import size); -1 when the mesh has no Hips bone.
inline float PreyBodyHeight(const APSLPreyCharacter& Prey)
{
    const USkeletalMeshComponent* Mesh = Prey.GetMesh();
    const FName Hips(TEXT("Hips"));
    if (!Mesh || Mesh->GetBoneIndex(Hips) == INDEX_NONE)
    {
        return -1.0f;
    }
    const double CapsuleBottom = Prey.GetActorLocation().Z - Prey.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    return static_cast<float>(Mesh->GetBoneLocation(Hips, EBoneSpaces::WorldSpace).Z - CapsuleBottom);
}

inline EPSLDinoAction PreyActiveAction(const APSLPreyCharacter& Prey)
{
    const UPSLDinoAnimInstance* Anim = Cast<UPSLDinoAnimInstance>(Prey.GetMesh()->GetAnimInstance());
    return Anim ? Anim->GetActiveAction() : EPSLDinoAction::None;
}

// A dead prey must have collapsed: body height <= 0.6 x the standing height.
inline void AssertPreyCollapsed(FPSLPieCtx& Ctx, const APSLPreyCharacter& Prey, float AliveHeight, const TCHAR* When)
{
    const float Height = PreyBodyHeight(Prey);
    Ctx.Test->AddInfo(FString::Printf(TEXT("%s: prey body height %.1f, alive %.1f"), When, Height, AliveHeight));
    Ctx.Test->TestTrue(
        FString::Printf(TEXT("Dead prey collapsed %s (height %.1f <= 0.6 x alive %.1f)"), When, Height, AliveHeight),
        Height >= 0.0f && Height <= 0.6f * AliveHeight);
}

// Clicks a widget. With a rendered viewport (non-zero cached geometry) this is a real Slate left click at the widget's
// centre; under -nullrhi geometry is zero and synthetic Slate input is not routed, so a UButton's OnClicked is
// broadcast instead and the fallback is logged. bOutRealInput reports which path ran.
inline bool ClickWidget(FPSLPieCtx& Context, UWidget* Widget, bool* bOutRealInput = nullptr)
{
    if (bOutRealInput)
    {
        *bOutRealInput = false;
    }
    if (!Widget || !Widget->GetIsEnabled() || !Widget->IsVisible())
    {
        return false;
    }

    const FGeometry Geometry = Widget->GetCachedGeometry();
    const FVector2f Size = Geometry.GetLocalSize();
    if (FApp::CanEverRender() && FSlateApplication::IsInitialized() && Size.X > 0.0f && Size.Y > 0.0f)
    {
        const FVector2f Center = Geometry.LocalToAbsolute(Size * 0.5f);
        FSlateApplication& Slate = FSlateApplication::Get();
        const FModifierKeysState NoModifiers;
        const TSet<FKey> NoButtons;
        TSet<FKey> LeftDown;
        LeftDown.Add(EKeys::LeftMouseButton);
        Slate.ProcessMouseMoveEvent(FPointerEvent(0, 0, Center, Center, NoButtons, FKey(), 0.0f, NoModifiers));
        Slate.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, 0, Center, Center, LeftDown, EKeys::LeftMouseButton, 0.0f, NoModifiers));
        Slate.ProcessMouseButtonUpEvent(FPointerEvent(0, 0, Center, Center, NoButtons, EKeys::LeftMouseButton, 0.0f, NoModifiers));
        if (bOutRealInput)
        {
            *bOutRealInput = true;
        }
        return true;
    }

    if (UButton* Button = Cast<UButton>(Widget))
    {
        UE_LOG(LogTemp, Log, TEXT("[fallback] ClickWidget %s: no geometry/renderer, broadcasting OnClicked"), *Widget->GetName());
        Button->OnClicked.Broadcast();
        return true;
    }
    return false;
}

// Sends a key press + release to the Slate focus path (UI-only screens: Pause, Game Over, Species). Under -nullrhi
// Slate may not route it; callers must then fall back to the widget's own methods.
inline bool PressKeyUI(FPSLPieCtx& Context, const FKey& Key)
{
    if (!FSlateApplication::IsInitialized())
    {
        return false;
    }
    FSlateApplication& Slate = FSlateApplication::Get();
    const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
    const bool bDown = Slate.ProcessKeyDownEvent(Event);
    Slate.ProcessKeyUpEvent(Event);
    return bDown;
}

struct FPredatorRun
{
    TWeakObjectPtr<APSLPredatorCharacter> Predator;
    int32 Seen = 0; // transitions already consumed
    double StepStart = -1.0;
    float HealthBefore = 0.0f;
};

inline APSLPredatorAIController* AIOf(const FPredatorRun& Run)
{
    return Run.Predator.IsValid() ? Cast<APSLPredatorAIController>(Run.Predator->GetController()) : nullptr;
}

// Consumes recorded transitions; true once one enters To (and, if given, with Reason).
inline bool ConsumeUntil(FPredatorRun& Run, EPSLPredatorState To, const TCHAR* Reason = nullptr)
{
    const APSLPredatorAIController* AI = AIOf(Run);
    if (!AI)
    {
        return false;
    }
    const TArray<FPSLPredatorTransition>& All = AI->GetTransitions();
    while (Run.Seen < All.Num())
    {
        const FPSLPredatorTransition& T = All[Run.Seen++];
        if (T.To == To && (!Reason || T.Reason == Reason))
        {
            return true;
        }
    }
    return false;
}

inline FString DumpTransitions(const FPredatorRun& Run)
{
    FString Out;
    if (const APSLPredatorAIController* AI = AIOf(Run))
    {
        for (const FPSLPredatorTransition& T : AI->GetTransitions())
        {
            Out += FString::Printf(TEXT("[%.2f %d->%d %s] "), T.WorldTime, int32(T.From), int32(T.To), *T.Reason);
        }
    }
    return Out;
}

// Moved verbatim from PSLLostValleyPieTests.cpp (static -> inline) so other valley tests share them.
inline bool GroundAt(UWorld* World, double X, double Y, FVector& Out)
{
    TArray<FHitResult> Hits;
    if (World && World->LineTraceMultiByChannel(Hits, FVector(X, Y, 8000.0), FVector(X, Y, -8000.0), ECC_Visibility))
    {
        for (const FHitResult& Hit : Hits)
        {
            if (Cast<ALandscapeProxy>(Hit.GetActor()))
            {
                Out = Hit.ImpactPoint + FVector(0.0, 0.0, 120.0);
                return true;
            }
        }
    }
    return false;
}

inline void TeleportTo(FPSLPieCtx& Ctx, const FVector& Location)
{
    Ctx.Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
    Ctx.Character->GetCharacterMovement()->StopMovementImmediately();
}

inline void FaceYawToward(FPSLPieCtx& Ctx, const FVector& Target)
{
    const FRotator Yaw(0.0, (Target - Ctx.Character->GetActorLocation()).GetSafeNormal2D().Rotation().Yaw, 0.0);
    Ctx.PC->SetControlRotation(Yaw);
    Ctx.Character->SetActorRotation(Yaw);
}

// Places the player Distance (2D) from Center on the first of 8 directions whose ground Z is within 300 of the target Z
// and, when SightFrom is set, that has a clear visibility line to SightFrom.
inline bool PlaceNear(FPSLPieCtx& Ctx, const FVector& Center, double Distance, const AActor* SightFrom = nullptr)
{
    for (int32 Index = 0; Index < 8; ++Index)
    {
        const double Angle = FMath::DegreesToRadians(45.0 * Index);
        FVector Ground;
        if (GroundAt(Ctx.World, Center.X + Distance * FMath::Cos(Angle), Center.Y + Distance * FMath::Sin(Angle), Ground)
            && FMath::Abs(Ground.Z - Center.Z) <= 300.0)
        {
            if (SightFrom)
            {
                FCollisionQueryParams Params;
                Params.AddIgnoredActor(SightFrom);
                Params.AddIgnoredActor(Ctx.Character.Get());
                FHitResult Hit;
                if (Ctx.World->LineTraceSingleByChannel(Hit, SightFrom->GetActorLocation() + FVector(0.0, 0.0, 60.0), Ground, ECC_Visibility, Params))
                {
                    continue;
                }
            }
            TeleportTo(Ctx, Ground);
            FaceYawToward(Ctx, Center);
            return true;
        }
    }
    return false;
}

inline TArray<APSLPreyCharacter*> GetPrey(UWorld* World)
{
    TArray<APSLPreyCharacter*> Prey;
    for (TActorIterator<APSLPreyCharacter> It(World); It; ++It)
    {
        Prey.Add(*It);
    }
    return Prey;
}

// True when the cvar now holds Value (a console/ini set outranks ECVF_SetByCode and would be kept).
inline bool SetPredatorSpawnCvar(int32 Value)
{
    IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("psl.Predator.Spawn"));
    if (V)
    {
        V->Set(Value, ECVF_SetByCode);
    }
    return V && V->GetInt() == Value;
}

inline bool SetIntCvar(const TCHAR* Name, int32 Value)
{
    IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
    if (V)
    {
        V->Set(Value, ECVF_SetByCode);
    }
    return V && V->GetInt() == Value;
}

inline int32 GetIntCvar(const TCHAR* Name)
{
    const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
    return V ? V->GetInt() : 0;
}

inline FString ShotPath(const FPSLPieCtx& Ctx, const FString& Label)
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("PIE"), Ctx.TestName + TEXT("_") + Label + TEXT(".png"));
}

// Reads the PIE game viewport (FViewport::ReadPixels flushes rendering) into a PNG at Path; bVerify also reloads it and asserts
// the size equals the viewport. Needs a renderer; callers guard with FApp::CanEverRender().
inline bool CapturePieViewport(FPSLPieCtx& Ctx, const FString& Label, const FString& Path, bool bVerify)
{
    FViewport* Viewport = Ctx.GameViewport->Viewport;
    if (!Viewport)
    {
        Ctx.MarkInvalid(FString::Printf(TEXT("PIE viewport missing for %s"), *Label));
        return false;
    }
    const FIntPoint Size = Viewport->GetSizeXY();
    TArray<FColor> Pixels;
    if (!Viewport->ReadPixels(Pixels) || Pixels.Num() != Size.X * Size.Y)
    {
        Ctx.Test->AddError(FString::Printf(TEXT("%s: PIE viewport ReadPixels failed (%d pixels for %dx%d)"), *Label, Pixels.Num(), Size.X, Size.Y));
        return false;
    }
    for (FColor& Pixel : Pixels)
    {
        Pixel.A = 255;
    }
    TArray64<uint8> Png;
    FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    if (!FFileHelper::SaveArrayToFile(Png, *Path))
    {
        Ctx.Test->AddError(FString::Printf(TEXT("%s: could not save %s"), *Label, *Path));
        return false;
    }
    if (bVerify)
    {
        FImage Saved;
        const bool bLoaded = FImageUtils::LoadImage(*Path, Saved);
        Ctx.Test->TestTrue(FString::Printf(TEXT("%s saved image %dx%d equals PIE viewport %dx%d"), *Label, Saved.SizeX, Saved.SizeY, Size.X, Size.Y),
            bLoaded && Saved.SizeX == Size.X && Saved.SizeY == Size.Y);
        Ctx.Test->AddInfo(FString::Printf(TEXT("PIE capture %s %dx%d"), *Path, Size.X, Size.Y));
    }
    return true;
}

// Waits 4 PIE ticks (>= 3 rendered frames after the camera/pose change), then captures the PIE viewport as <TestName>_<Label>.png.
inline void QueueCapture(const TSharedRef<FPSLPieCtx>& Context, const FString& Label)
{
    AddPieStep(Context, TEXT("Capture"), 10.0f, [Label, Frames = 0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable() || !FApp::CanEverRender())
        {
            return true;
        }
        if (++Frames < 4)
        {
            return false;
        }
        CapturePieViewport(Ctx, Label, ShotPath(Ctx, Label), true);
        Record(Ctx, *Label);
        return true;
    });
}

// Shared state of the still-shot steps: the temporary camera, the previous view target and the spawned old-T-Rex comparison pawn.
struct FShotState
{
    TWeakObjectPtr<ACameraActor> Cam;
    TWeakObjectPtr<AActor> PrevTarget;
    TWeakObjectPtr<APSLCharacter> Other;
    TWeakObjectPtr<APSLPreyCharacter> Prey; // HeroVideo bite prey: moved in front of the hero, put back (or destroyed if spawned) by RestoreView
    FVector PreyHome = FVector::ZeroVector;
    bool bPreyMoved = false;
    bool bPreySpawned = false; // no living prey existed: the video spawned one, RestoreView destroys it

    void RestoreView(FPSLPieCtx& Ctx)
    {
        if (Ctx.HasObjects())
        {
            Ctx.PC->SetViewTarget(PrevTarget.IsValid() ? PrevTarget.Get() : Ctx.Character.Get());
        }
        if (Cam.IsValid())
        {
            Cam->Destroy();
        }
        if (Other.IsValid())
        {
            Other->Destroy();
        }
        if (bPreyMoved && Prey.IsValid())
        {
            if (bPreySpawned)
            {
                Prey->Destroy();
            }
            else
            {
                Prey->SetActorLocation(PreyHome, false, nullptr, ETeleportType::TeleportPhysics);
            }
        }
        bPreyMoved = false;
        bPreySpawned = false;
    }
};

// Spawns the temporary camera (FOV 60) on first use, puts it at Spot looking at LookAt and makes it the view target.
inline void UseCamera(FPSLPieCtx& Ctx, FShotState& S, const FVector& Spot, const FVector& LookAt)
{
    if (!S.Cam.IsValid())
    {
        S.PrevTarget = Ctx.PC->GetViewTarget();
        S.Cam = Ctx.World->SpawnActor<ACameraActor>(Spot, FRotator::ZeroRotator);
        S.Cam->GetCameraComponent()->SetFieldOfView(60.0f);
    }
    S.Cam->SetActorLocationAndRotation(Spot, (LookAt - Spot).Rotation());
    Ctx.PC->SetViewTarget(S.Cam.Get());
}

// Distance at which a body of Width uu fills Fraction of a FOV 60 frame.
inline double FrameDistance(double Width, double Fraction)
{
    return Width / Fraction / (2.0 * FMath::Tan(FMath::DegreesToRadians(30.0)));
}

// Deterministic foliage-occlusion count: samples From->To every 100 uu with a 120 uu sphere and sums the instances of every
// UInstancedStaticMeshComponent in the world (HISM, foliage, landscape grass) that overlap it. Components owned by A or B are ignored.
// The foliage has no collision, so an ECC_Visibility trace cannot see it.
inline int32 OccluderCount(UWorld* World, const FVector& From, const FVector& To, const AActor* A, const AActor* B)
{
    TArray<const UInstancedStaticMeshComponent*> Comps;
    for (TObjectIterator<UInstancedStaticMeshComponent> It; It; ++It)
    {
        const AActor* Owner = It->GetOwner();
        if (It->GetWorld() == World && Owner != A && Owner != B)
        {
            Comps.Add(*It);
        }
    }
    const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist(From, To) / 100.0));
    int32 Count = 0;
    for (int32 I = 0; I <= Steps; ++I)
    {
        const FVector P = FMath::Lerp(From, To, double(I) / Steps);
        for (const UInstancedStaticMeshComponent* Comp : Comps)
        {
            Count += Comp->GetInstancesOverlappingSphere(P, 120.0f, true).Num();
        }
    }
    return Count;
}

} // namespace PSLPie

#endif
