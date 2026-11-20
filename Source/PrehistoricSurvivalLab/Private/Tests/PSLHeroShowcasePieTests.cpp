#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "PSLPieTestSupport.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstance.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraActor.h"
#include "PSLDinoSpecies.h"

namespace
{
using namespace PSLPie;

constexpr const TCHAR* HeroMapPath = TEXT("/Game/Maps/L_LostValley");
constexpr const TCHAR* HeroMeshPackage = TEXT("/Game/Dev/ArtDir/Hero/SK_HeroTrex");
constexpr const TCHAR* HeroDir = TEXT("/Game/Dev/ArtDir/Hero/");

bool SetIntCvar(const TCHAR* Name, int32 Value)
{
    IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
    if (V)
    {
        V->Set(Value, ECVF_SetByCode);
    }
    return V && V->GetInt() == Value;
}

// (a) mesh/anim/skeleton wiring and (b) materials; both run under -nullrhi.
void AssertHeroAssets(FPSLPieCtx& Ctx)
{
    FAutomationTestBase& T = *Ctx.Test;
    USkeletalMeshComponent* Mesh = Ctx.Character->GetMesh();
    const USkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
    T.TestTrue(TEXT("Player mesh is SK_HeroTrex"), Asset && Asset->GetPathName() == TEXT("/Game/Dev/ArtDir/Hero/SK_HeroTrex.SK_HeroTrex"));
    const UPSLDinoAnimInstance* Anim = Cast<UPSLDinoAnimInstance>(Mesh->GetAnimInstance());
    if (!T.TestNotNull(TEXT("Player anim instance is UPSLDinoAnimInstance"), Anim) || !Asset)
    {
        return;
    }
    const USkeleton* Skeleton = Asset->GetSkeleton();
    const TPair<const TCHAR*, const UAnimSequence*> Clips[] = {
        {TEXT("Idle"), Anim->IdleSequence}, {TEXT("Walk"), Anim->WalkSequence},
        {TEXT("Run"), Anim->RunSequence}, {TEXT("Attack"), Anim->AttackSequence}};
    for (const auto& Clip : Clips)
    {
        if (T.TestNotNull(FString::Printf(TEXT("%s clip set"), Clip.Key), Clip.Value))
        {
            T.TestTrue(FString::Printf(TEXT("%s clip skeleton equals mesh skeleton"), Clip.Key), Clip.Value->GetSkeleton() == Skeleton);
        }
    }
    T.TestNull(TEXT("DeathSequence is null"), Anim->DeathSequence.Get());

    const TArray<FSkeletalMaterial>& Slots = Asset->GetMaterials();
    T.TestTrue(TEXT("Mesh has material slots"), Slots.Num() > 0);
    for (int32 Index = 0; Index < Slots.Num(); ++Index)
    {
        const UMaterialInterface* Mat = Mesh->GetMaterial(Index);
        const FString Slot = Slots[Index].MaterialSlotName.ToString();
        if (!T.TestNotNull(FString::Printf(TEXT("Slot %s material non-null"), *Slot), Mat))
        {
            continue;
        }
        T.TestTrue(FString::Printf(TEXT("Slot %s is not the default material"), *Slot), Mat != UMaterial::GetDefaultMaterial(MD_Surface));
        T.TestTrue(FString::Printf(TEXT("Slot %s material under %s"), *Slot, HeroDir), Mat->GetPathName().StartsWith(HeroDir));
        // GetUsedTextures is empty without compiled shader maps (-nullrhi): union it with referenced textures and instance parameter overrides.
        TArray<UTexture*> Textures;
        Mat->GetUsedTextures(Textures, EMaterialQualityLevel::Num, true, ERHIFeatureLevel::Num, true);
        for (UObject* Ref : Mat->GetReferencedTextures())
        {
            if (UTexture* Tex = Cast<UTexture>(Ref))
            {
                Textures.AddUnique(Tex);
            }
        }
        for (const UMaterialInterface* Link = Mat; Link; )
        {
            const UMaterialInstance* Inst = Cast<UMaterialInstance>(Link);
            if (!Inst)
            {
                break;
            }
            for (const FTextureParameterValue& Param : Inst->TextureParameterValues)
            {
                if (Param.ParameterValue)
                {
                    Textures.AddUnique(Param.ParameterValue);
                }
            }
            Link = Inst->Parent;
        }
        int32 HeroTextures = 0;
        FString Names;
        for (const UTexture* Tex : Textures)
        {
            HeroTextures += Tex && Tex->GetPathName().StartsWith(HeroDir) ? 1 : 0;
            Names += (Tex ? Tex->GetName() : FString(TEXT("null"))) + TEXT(" ");
        }
        T.AddInfo(FString::Printf(TEXT("Slot %s -> %s -> %s"), *Slot, *Mat->GetPathName(), *Names));
        if (Slot.Contains(TEXT("Body"), ESearchCase::IgnoreCase) || Slot.Contains(TEXT("Head"), ESearchCase::IgnoreCase))
        {
            T.TestTrue(FString::Printf(TEXT("Slot %s uses a hero texture"), *Slot), HeroTextures > 0);
        }
    }
}

// Per side, tracks every DEF-toe*/DEF-metatarsus* bone. Per frame the foot speed is the horizontal world speed of the lowest-Z
// bone (the contact point); the minimum over the last clip cycle is ~0 for a planted foot.
struct FFootTrack
{
    TArray<FName> Bones[2];
    TArray<FVector> Last[2];
    TArray<TPair<double, double>> Speed[2]; // (time, speed)
    double LastTime = -1.0;

    void Init(const USkeletalMeshComponent& Mesh)
    {
        for (int32 I = 0; I < Mesh.GetNumBones(); ++I)
        {
            const FString Name = Mesh.GetBoneName(I).ToString();
            if (Name.StartsWith(TEXT("DEF-toe"), ESearchCase::IgnoreCase) || Name.StartsWith(TEXT("DEF-metatarsus"), ESearchCase::IgnoreCase))
            {
                const int32 Side = Name.EndsWith(TEXT("L"), ESearchCase::IgnoreCase) ? 0 : (Name.EndsWith(TEXT("R"), ESearchCase::IgnoreCase) ? 1 : -1);
                if (Side >= 0)
                {
                    Bones[Side].Add(Mesh.GetBoneName(I));
                    Last[Side].Add(FVector::ZeroVector);
                }
            }
        }
    }

    void Sample(const USkeletalMeshComponent& Mesh, double Now)
    {
        const bool bHavePrev = LastTime >= 0.0 && Now > LastTime;
        for (int32 Side = 0; Side < 2; ++Side)
        {
            double LowestZ = TNumericLimits<double>::Max();
            double ContactSpeed = 0.0;
            for (int32 B = 0; B < Bones[Side].Num(); ++B)
            {
                const FVector P = Mesh.GetBoneLocation(Bones[Side][B], EBoneSpaces::WorldSpace);
                if (P.Z < LowestZ)
                {
                    LowestZ = P.Z;
                    ContactSpeed = FVector::Dist2D(P, Last[Side][B]) / FMath::Max(Now - LastTime, KINDA_SMALL_NUMBER);
                }
                Last[Side][B] = P;
            }
            if (bHavePrev && Bones[Side].Num() > 0)
            {
                Speed[Side].Add({Now, ContactSpeed});
            }
        }
        LastTime = Now;
    }

    // Minimum contact speed per foot over [End - BaseCycle / Rate, End]; asserts min/ground <= 0.25 and logs the anim speed that would zero it.
    void Report(FPSLPieCtx& Ctx, const TCHAR* Label, double End, double BaseCycle, double Rate, double GroundSpeed)
    {
        const double Cycle = BaseCycle / FMath::Max(Rate, 0.01);
        double Min[2] = {-1.0, -1.0};
        for (int32 Side = 0; Side < 2; ++Side)
        {
            for (const TPair<double, double>& S : Speed[Side])
            {
                if (S.Key >= End - Cycle)
                {
                    Min[Side] = Min[Side] < 0.0 ? S.Value : FMath::Min(Min[Side], S.Value);
                }
            }
            Speed[Side].Reset();
        }
        const TCHAR* SideName[2] = {TEXT("L"), TEXT("R")};
        for (int32 Side = 0; Side < 2; ++Side)
        {
            const double Ratio = GroundSpeed > 1.0 && Min[Side] >= 0.0 ? Min[Side] / GroundSpeed : 1.0;
            const double Suggested = (GroundSpeed - FMath::Max(Min[Side], 0.0)) / FMath::Max(Rate, 0.01);
            const FString Line = FString::Printf(
                TEXT("{\"step\":\"%s_foot_slide\",\"foot\":\"%s\",\"ground_speed\":%.1f,\"play_rate\":%.3f,\"cycle\":%.2f,\"min_foot_speed\":%.2f,\"ratio\":%.4f,\"suggested_anim_speed\":%.1f}"),
                Label, SideName[Side], GroundSpeed, Rate, Cycle, Min[Side], Ratio, Suggested);
            Ctx.Measurements.Add(Line);
            Ctx.Test->AddInfo(Line);
            Ctx.Test->TestTrue(FString::Printf(TEXT("%s foot %s slide ratio %.3f <= 0.25 (min %.1f of %.1f uu/s)"), Label, SideName[Side], Ratio, Min[Side], GroundSpeed), Ratio <= 0.25);
        }
    }
};

struct FHeroMoveState
{
    int32 Phase = 0;
    double PhaseStart = 0.0;
    double TurnFrom = 0.0;
    FFootTrack Feet;

    // Mirrors FPSLDinoAnimProxy::Update: rate 1 when the species has no AnimSpeed.
    static double WalkRate(FPSLPieCtx& Ctx, double V)
    {
        const UPSLDinoAnimInstance* A = Cast<UPSLDinoAnimInstance>(Ctx.Character->GetMesh()->GetAnimInstance());
        return A && A->WalkAnimSpeed > 0.0f ? FMath::Clamp(V / A->WalkAnimSpeed, 0.5, 3.5) : 1.0;
    }
    static double RunRate(FPSLPieCtx& Ctx, double V)
    {
        const UPSLDinoAnimInstance* A = Cast<UPSLDinoAnimInstance>(Ctx.Character->GetMesh()->GetAnimInstance());
        return A && A->RunAnimSpeed > 0.0f ? FMath::Clamp(V / A->RunAnimSpeed, 0.5, 3.0) : 1.0;
    }

    static double Speed(FPSLPieCtx& Ctx) { return Ctx.Character->GetVelocity().Size2D(); }

    bool Tick(FPSLPieCtx& Ctx)
    {
        USkeletalMeshComponent* Mesh = Ctx.Character->GetMesh();
        const double Now = Ctx.World->GetTimeSeconds();
        if (Phase == 0)
        {
            Feet.Init(*Mesh);
            Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            SendKey(Ctx, EKeys::W, IE_Pressed);
            PhaseStart = Now;
            Phase = 1;
            return false;
        }
        Feet.Sample(*Mesh, Now);
        if (Phase == 1 && Now - PhaseStart >= 3.0)
        {
            const double V = Speed(Ctx);
            Ctx.Test->TestTrue(FString::Printf(TEXT("W held 3 s reaches walk speed (%.0f >= 250)"), V), V >= 250.0);
            Feet.Report(Ctx, TEXT("walk"), Now, 3.0, WalkRate(Ctx, V), V);
            Record(Ctx, TEXT("walk"));
            SendKey(Ctx, EKeys::LeftShift, IE_Pressed);
            PhaseStart = Now;
            Phase = 2;
        }
        else if (Phase == 2 && Now - PhaseStart >= 3.0)
        {
            const double V = Speed(Ctx);
            Ctx.Test->TestTrue(FString::Printf(TEXT("W+Shift held 3 s reaches sprint speed (%.0f >= 500)"), V), V >= 500.0);
            Feet.Report(Ctx, TEXT("run"), Now, 1.2, RunRate(Ctx, V), V);
            Record(Ctx, TEXT("sprint"));
            TurnFrom = Ctx.PC->GetControlRotation().Yaw;
            PhaseStart = Now;
            Phase = 3;
        }
        else if (Phase == 3)
        {
            const double Alpha = FMath::Clamp((Now - PhaseStart) / 2.0, 0.0, 1.0);
            Ctx.PC->SetControlRotation(FRotator(0.0, TurnFrom + 180.0 * Alpha, 0.0));
            if (Alpha >= 1.0)
            {
                const double Diff = FMath::Abs(FRotator::NormalizeAxis(Ctx.Character->GetActorRotation().Yaw - (TurnFrom + 180.0)));
                Ctx.Test->TestTrue(FString::Printf(TEXT("Actor yaw follows the 180 deg turn within 30 deg (off by %.1f)"), Diff), Diff <= 30.0);
                Record(Ctx, TEXT("turned"));
                SendKey(Ctx, EKeys::LeftShift, IE_Released);
                SendKey(Ctx, EKeys::W, IE_Released);
                return true;
            }
        }
        return false;
    }
};

struct FHeroBiteState
{
    int32 Phase = 0;
    double PressTime = 0.0;
    double EndTime = -1.0;
    bool bSawAttack = false;
    float PreyBefore = 0.0f;
    float PlayerBefore = 0.0f;
    TWeakObjectPtr<APSLPreyCharacter> Prey;

    bool Tick(FPSLPieCtx& Ctx)
    {
        const double Now = Ctx.World->GetTimeSeconds();
        if (Phase == 0)
        {
            const TArray<APSLPreyCharacter*> All = GetPrey(Ctx.World);
            if (All.Num() == 0 || !All[0])
            {
                Ctx.MarkInvalid(TEXT("No prey for the bite test"));
                return true;
            }
            Prey = All[0];
            if (!PlaceNear(Ctx, Prey->GetActorLocation(), 180.0))
            {
                Ctx.MarkInvalid(TEXT("No ground placement 180 uu from prey"));
                return true;
            }
            HoldPreyStill(Ctx, *Prey);
            PreyBefore = Prey->GetStatsComponent()->GetHealth();
            SendKey(Ctx, EKeys::LeftMouseButton, IE_Pressed);
            PressTime = Now;
            Phase = 1;
            return false;
        }
        if (!Prey.IsValid())
        {
            Ctx.MarkInvalid(TEXT("Prey vanished during bite test"));
            return true;
        }
        HoldPreyStill(Ctx, *Prey);
        if (Phase == 1)
        {
            SendKey(Ctx, EKeys::LeftMouseButton, IE_Released);
            Phase = 2;
        }
        const bool bAttacking = PlayerActiveAction(Ctx) == EPSLDinoAction::Attack;
        if (bAttacking && !bSawAttack)
        {
            bSawAttack = true;
            Ctx.Test->TestTrue(FString::Printf(TEXT("ActiveAction == Attack within 0.2 s (%.3f s)"), Now - PressTime), Now - PressTime <= 0.2);
        }
        if (!bSawAttack && Now - PressTime > 0.2)
        {
            Ctx.Test->AddError(TEXT("ActiveAction never became Attack within 0.2 s of the bite"));
            bSawAttack = true; // report once
        }
        if (Phase == 2 && bSawAttack && !bAttacking && Now - PressTime > 0.25)
        {
            EndTime = Now - PressTime;
            Ctx.Test->TestTrue(FString::Printf(TEXT("Attack action ends within %.2f s (%.2f s)"), 4.0 / 3.0 + 0.3, EndTime), EndTime <= 4.0 / 3.0 + 0.3);
            Ctx.Test->TestTrue(TEXT("Prey health dropped after the bite"), Prey->GetStatsComponent()->GetHealth() < PreyBefore);
            PlayerBefore = Ctx.Character->GetStatsComponent()->GetHealth();
            UGameplayStatics::ApplyDamage(Ctx.Character.Get(), 10.0f, nullptr, nullptr, UDamageType::StaticClass());
            Ctx.Test->TestTrue(FString::Printf(TEXT("Player health dropped by 10 (%.1f -> %.1f)"), PlayerBefore, Ctx.Character->GetStatsComponent()->GetHealth()),
                FMath::IsNearlyEqual(Ctx.Character->GetStatsComponent()->GetHealth(), PlayerBefore - 10.0f, 0.5f));
            Record(Ctx, TEXT("bitten"));
            return true;
        }
        if (Now - PressTime > 4.0)
        {
            Ctx.MarkInvalid(TEXT("Attack action did not end within 4 s"));
            return true;
        }
        return false;
    }
};

// Shared state of the still-shot steps: the temporary camera, the previous view target and the spawned old-T-Rex comparison pawn.
struct FShotState
{
    TWeakObjectPtr<ACameraActor> Cam;
    TWeakObjectPtr<AActor> PrevTarget;
    TWeakObjectPtr<APSLCharacter> Other;

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
    }
};

bool LineClear(FPSLPieCtx& Ctx, const FShotState& S, const FVector& From, const FVector& To)
{
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Ctx.Character.Get());
    if (S.Other.IsValid())
    {
        Params.AddIgnoredActor(S.Other.Get());
    }
    FHitResult Hit;
    return !Ctx.World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params);
}

// Puts the temporary camera (FOV 60) Offset from Target on the left, then right, then each raised by 150, taking the first
// spot with a clear ECC_Visibility line to Target (else the first), looks at LookAt and makes it the view target.
void PlaceCamera(FPSLPieCtx& Ctx, FShotState& S, const FVector& Target, const FVector& LookAt, const FVector& OffsetLeft, const FVector& OffsetRight)
{
    FVector Spot = Target + OffsetLeft;
    bool bFound = false;
    for (const double Raise : {0.0, 150.0})
    {
        for (const FVector& Offset : {OffsetLeft, OffsetRight})
        {
            const FVector Try = Target + Offset + FVector(0.0, 0.0, Raise);
            if (LineClear(Ctx, S, Try, Target))
            {
                Spot = Try;
                bFound = true;
                break;
            }
        }
        if (bFound)
        {
            break;
        }
    }
    if (!S.Cam.IsValid())
    {
        S.PrevTarget = Ctx.PC->GetViewTarget();
        S.Cam = Ctx.World->SpawnActor<ACameraActor>(Spot, FRotator::ZeroRotator);
        S.Cam->GetCameraComponent()->SetFieldOfView(60.0f);
    }
    S.Cam->SetActorLocationAndRotation(Spot, (LookAt - Spot).Rotation());
    Ctx.PC->SetViewTarget(S.Cam.Get());
    Ctx.Test->AddInfo(FString::Printf(TEXT("Camera at %s, clear line %s"), *Spot.ToString(), bFound ? TEXT("yes") : TEXT("no (fallback)")));
}

// Waits Seconds of world time (pawn idle) before the next step.
void QueueHold(const TSharedRef<FPSLPieCtx>& Context, double Seconds)
{
    AddPieStep(Context, TEXT("Hold"), 5.0f + Seconds, [Seconds, Start = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        Start = Start < 0.0 ? Now : Start;
        return Now - Start >= Seconds;
    });
}

// Distance at which a body of Width uu fills Fraction of a FOV 60 frame.
double FrameDistance(double Width, double Fraction)
{
    return Width / Fraction / (2.0 * FMath::Tan(FMath::DegreesToRadians(30.0)));
}

double BodyLength(const FPSLPieCtx& Ctx)
{
    const FVector Extent = Ctx.Character->GetMesh()->Bounds.BoxExtent;
    return 2.0 * FMath::Max(Extent.X, Extent.Y);
}

// Side shot camera for the player: perpendicular to the actor forward, body length ~60% of the frame width, aimed at the bounds centre.
void SideShot(FPSLPieCtx& Ctx, FShotState& S)
{
    const FVector Centre = Ctx.Character->GetMesh()->Bounds.Origin;
    const FVector Side = Ctx.Character->GetActorRightVector() * FrameDistance(BodyLength(Ctx), 0.6);
    PlaceCamera(Ctx, S, Centre, Centre, -Side, Side);
}

// Fixed-step state shared by the video step and Cleanup so the timestep is always restored.
struct FTimestepGuard
{
    bool bActive = false;
    bool bPrevFixed = false;
    double PrevDelta = 0.0;

    void Enter()
    {
        if (!bActive)
        {
            bActive = true;
            bPrevFixed = FApp::UseFixedTimeStep();
            PrevDelta = FApp::GetFixedDeltaTime();
            FApp::SetUseFixedTimeStep(true);
            FApp::SetFixedDeltaTime(1.0 / 30.0);
        }
    }
    void Restore()
    {
        if (bActive)
        {
            bActive = false;
            FApp::SetUseFixedTimeStep(bPrevFixed);
            FApp::SetFixedDeltaTime(PrevDelta);
        }
    }
};

// HeroVideo: scripted idle / walk / run / turn / stop / bite at 30 fps fixed step; one PNG per tick plus frames.json.
struct FVideoState
{
    TSharedRef<FTimestepGuard> Guard;
    int32 Phase = -1;
    double PhaseStart = 0.0;
    double TurnFrom = 0.0;
    FString Dir;
    TArray<TPair<int32, double>> Frames;

    explicit FVideoState(TSharedRef<FTimestepGuard> InGuard) : Guard(MoveTemp(InGuard)) {}

    void Finish(FPSLPieCtx& Ctx)
    {
        FString Json = TEXT("{\"frames\":[");
        for (int32 I = 0; I < Frames.Num(); ++I)
        {
            Json += FString::Printf(TEXT("%s{\"i\":%d,\"t\":%.4f}"), I ? TEXT(",") : TEXT(""), Frames[I].Key, Frames[I].Value);
        }
        Json += TEXT("]}");
        IFileManager::Get().MakeDirectory(*Dir, true);
        FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Dir, TEXT("frames.json")));
        const double Span = Frames.Num() > 1 ? Frames.Last().Value - Frames[0].Value : 0.0;
        Ctx.Test->AddInfo(FString::Printf(TEXT("HeroVideo frames %d, mean dt %.4f s"), Frames.Num(), Frames.Num() > 1 ? Span / (Frames.Num() - 1) : 0.0));
        Guard->Restore();
    }

    bool Tick(FPSLPieCtx& Ctx)
    {
        const double Now = Ctx.World->GetTimeSeconds();
        if (Phase < 0)
        {
            Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("PIE"), TEXT("HeroVideo"));
            Guard->Enter();
            PhaseStart = Now;
            Phase = 0;
        }
        if (FApp::CanEverRender() && !FScreenshotRequest::IsScreenshotRequested())
        {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("frame_%04d.png"), Frames.Num())), false, false);
            Frames.Add({Frames.Num(), Now});
        }
        const double In = Now - PhaseStart;
        auto Next = [&](int32 P) { Phase = P; PhaseStart = Now; };
        switch (Phase)
        {
        case 0: // idle 1 s
            if (In >= 1.0) { SendKey(Ctx, EKeys::W, IE_Pressed); Next(1); }
            break;
        case 1: // W 3 s
            if (In >= 3.0) { SendKey(Ctx, EKeys::LeftShift, IE_Pressed); Next(2); }
            break;
        case 2: // W+Shift 3 s
            if (In >= 3.0) { TurnFrom = Ctx.PC->GetControlRotation().Yaw; Next(3); }
            break;
        case 3: // turn +180 over 2 s while running
            Ctx.PC->SetControlRotation(FRotator(0.0, TurnFrom + 180.0 * FMath::Clamp(In / 2.0, 0.0, 1.0), 0.0));
            if (In >= 2.0) { SendKey(Ctx, EKeys::LeftShift, IE_Released); SendKey(Ctx, EKeys::W, IE_Released); Next(4); }
            break;
        case 4: // stop 1.5 s
            if (In >= 1.5) { Ctx.Character->TryBite(); Next(5); }
            break;
        default: // bite + 1.5 s
            if (In >= 1.5) { Finish(Ctx); return true; }
            break;
        }
        return false;
    }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLHeroShowcaseTest, "PSL.Dev.HeroTrex.Showcase", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLHeroShowcaseTest::RunTest(const FString& Parameters)
{
    if (!FPackageName::DoesPackageExist(HeroMeshPackage))
    {
        AddWarning(TEXT("hero assets absent: showcase skipped"));
        return true;
    }
    if (!SetIntCvar(TEXT("psl.Dev.HeroTrex"), 1))
    {
        AddWarning(TEXT("hero assets absent: showcase skipped (psl.Dev.HeroTrex could not be set)"));
        SetIntCvar(TEXT("psl.Dev.HeroTrex"), 0);
        return true;
    }
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("HeroShowcase"));
    if (!SetPredatorSpawnCvar(1) || !QueuePieStart(*this, Context, HeroMapPath))
    {
        SetIntCvar(TEXT("psl.Dev.HeroTrex"), 0);
        return false;
    }
    AddPieStep(Context, TEXT("IgnoreLookInput"), 2.0f, [](FPSLPieCtx& Ctx)
    {
        if (Ctx.IsUsable())
        {
            Ctx.PC->SetIgnoreLookInput(true);
        }
        return true;
    });
    AddPieStep(Context, TEXT("HeroAssets"), 10.0f, [](FPSLPieCtx& Ctx)
    {
        if (Ctx.IsUsable())
        {
            AssertHeroAssets(Ctx);
        }
        return true;
    });
    // (c) facing, feet, scale; (d) camera. Bones are refreshed without rendering via AlwaysTickPoseAndRefreshBones.
    AddPieStep(Context, TEXT("PoseAndCamera"), 10.0f, [Frames = 0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        USkeletalMeshComponent* Mesh = Ctx.Character->GetMesh();
        Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        if (++Frames < 30)
        {
            return false;
        }
        FAutomationTestBase& T = *Ctx.Test;
        const FName Head(TEXT("DEF-head"));
        const FName Pelvis(TEXT("DEF-pelvis"));
        if (!T.TestTrue(TEXT("DEF-head and DEF-pelvis bones exist"), Mesh->GetBoneIndex(Head) != INDEX_NONE && Mesh->GetBoneIndex(Pelvis) != INDEX_NONE))
        {
            return true;
        }
        const FVector Along = (Mesh->GetBoneLocation(Head, EBoneSpaces::WorldSpace) - Mesh->GetBoneLocation(Pelvis, EBoneSpaces::WorldSpace)).GetSafeNormal2D();
        const FVector Forward = Ctx.Character->GetActorForwardVector().GetSafeNormal2D();
        const double Dot = FVector::DotProduct(Along, Forward);
        const double YawOffset = FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X)) - Forward.Rotation().Yaw;
        T.AddInfo(FString::Printf(TEXT("Facing dot %.3f, measured yaw offset %.1f deg (MeshYaw %.1f)"), Dot, FRotator::NormalizeAxis(YawOffset), Mesh->GetRelativeRotation().Yaw));
        T.TestTrue(FString::Printf(TEXT("Hero faces actor forward (dot %.3f > 0.7)"), Dot), Dot > 0.7);

        const double CapsuleBottom = Ctx.Character->GetActorLocation().Z - Ctx.Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        double LowestZ = TNumericLimits<double>::Max();
        for (int32 Bone = 0; Bone < Mesh->GetNumBones(); ++Bone)
        {
            const FString Name = Mesh->GetBoneName(Bone).ToString();
            if (Name.StartsWith(TEXT("DEF-toe")) || Name.StartsWith(TEXT("DEF-metatarsus")))
            {
                LowestZ = FMath::Min(LowestZ, Mesh->GetBoneLocation(Mesh->GetBoneName(Bone), EBoneSpaces::WorldSpace).Z);
            }
        }
        if (T.TestTrue(TEXT("Foot bones found"), LowestZ < TNumericLimits<double>::Max()))
        {
            T.AddInfo(FString::Printf(TEXT("Lowest foot bone Z %.1f, capsule bottom %.1f"), LowestZ, CapsuleBottom));
            T.TestTrue(FString::Printf(TEXT("Feet within 30 uu of capsule bottom (delta %.1f)"), LowestZ - CapsuleBottom), FMath::Abs(LowestZ - CapsuleBottom) <= 30.0);
        }

        const double Scale = Mesh->GetRelativeScale3D().X;
        T.AddInfo(FString::Printf(TEXT("Relative mesh scale %.4f"), Scale));
        T.TestTrue(TEXT("Relative scale in [0.2, 1.0]"), Scale >= 0.2 && Scale <= 1.0);

        // (d) third-person camera must not sit inside the mesh bounds.
        const UCameraComponent* Cam = Ctx.Character->FindComponentByClass<UCameraComponent>();
        if (T.TestNotNull(TEXT("Follow camera"), Cam))
        {
            const FVector CamLoc = Cam->GetComponentLocation();
            const bool bInside = Mesh->Bounds.GetBox().IsInside(CamLoc);
            T.AddInfo(FString::Printf(TEXT("Camera distance %.1f, inside mesh bounds: %s"), FVector::Dist(CamLoc, Ctx.Character->GetActorLocation()), bInside ? TEXT("yes") : TEXT("no")));
            T.TestFalse(TEXT("Camera is outside the mesh bounds box"), bInside);
        }
        Record(Ctx, TEXT("pose"));
        return true;
    });
    // (e) movement: walk 3 s, sprint 3 s, then turn 180 deg over 2 s while running. Foot-slide is measured, not asserted.
    AddPieStep(Context, TEXT("Movement"), 40.0f, [S = MakeShared<FHeroMoveState>()](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        return S->Tick(Ctx);
    });
    // (f) bite and damage.
    AddPieStep(Context, TEXT("BiteAndDamage"), 30.0f, [S = MakeShared<FHeroBiteState>()](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        return S->Tick(Ctx);
    });
    // Stills and video need a renderer; under -nullrhi only the video script (without capture) runs.
    const TSharedRef<FShotState> Shot = MakeShared<FShotState>();
    const TSharedRef<FTimestepGuard> Guard = MakeShared<FTimestepGuard>();
    if (FApp::CanEverRender())
    {
        QueueHold(Context, 1.0);
        AddPieStep(Context, TEXT("hero_side_full"), 5.0f, [Shot](FPSLPieCtx& Ctx)
        {
            if (Ctx.IsUsable())
            {
                SideShot(Ctx, *Shot);
                RequestScreenshot(Ctx, TEXT("hero_side_full"));
            }
            return true;
        });
        QueueAwaitScreenshot(Context, TEXT("hero_side_full"));
        AddPieStep(Context, TEXT("hero_head_closeup"), 5.0f, [Shot](FPSLPieCtx& Ctx)
        {
            if (Ctx.IsUsable())
            {
                const USkeletalMeshComponent* Mesh = Ctx.Character->GetMesh();
                const FVector Head = Mesh->GetBoneLocation(TEXT("DEF-head"), EBoneSpaces::WorldSpace);
                const FVector Fwd = Ctx.Character->GetActorForwardVector() * FMath::Cos(FMath::DegreesToRadians(40.0));
                const FVector Side = Ctx.Character->GetActorRightVector() * FMath::Sin(FMath::DegreesToRadians(40.0));
                PlaceCamera(Ctx, *Shot, Head, Head, (Fwd - Side) * 200.0, (Fwd + Side) * 200.0);
                RequestScreenshot(Ctx, TEXT("hero_head_closeup"));
            }
            return true;
        });
        QueueAwaitScreenshot(Context, TEXT("hero_head_closeup"));
        // Walking third-person shot through the pawn camera.
        AddPieStep(Context, TEXT("hero_thirdperson"), 10.0f, [Shot, Start = -1.0](FPSLPieCtx& Ctx) mutable
        {
            if (!Ctx.IsUsable())
            {
                return true;
            }
            const double Now = Ctx.World->GetTimeSeconds();
            if (Start < 0.0)
            {
                Shot->RestoreView(Ctx);
                SendKey(Ctx, EKeys::W, IE_Pressed);
                Start = Now;
            }
            else if (Now - Start >= 1.5)
            {
                RequestScreenshot(Ctx, TEXT("hero_thirdperson"));
                return true;
            }
            return false;
        });
        QueueAwaitScreenshot(Context, TEXT("hero_thirdperson"));
        AddPieStep(Context, TEXT("ReleaseW"), 2.0f, [](FPSLPieCtx& Ctx)
        {
            SendKey(Ctx, EKeys::W, IE_Released);
            return true;
        });
        QueueHold(Context, 1.0);
        AddPieStep(Context, TEXT("hero_attack_side"), 5.0f, [Shot, Start = -1.0](FPSLPieCtx& Ctx) mutable
        {
            if (!Ctx.IsUsable())
            {
                return true;
            }
            const double Now = Ctx.World->GetTimeSeconds();
            if (Start < 0.0)
            {
                SideShot(Ctx, *Shot);
                Ctx.Test->TestTrue(TEXT("TryBite for the attack shot"), Ctx.Character->TryBite());
                Start = Now;
            }
            else if (Now - Start >= 0.45)
            {
                RequestScreenshot(Ctx, TEXT("hero_attack_side"));
                return true;
            }
            return false;
        });
        QueueAwaitScreenshot(Context, TEXT("hero_attack_side"));
        QueueHold(Context, 1.5);
        // Old (SK_Trex) T-Rex 700 uu to the player's right, one camera framing both.
        AddPieStep(Context, TEXT("old_vs_new"), 5.0f, [Shot](FPSLPieCtx& Ctx)
        {
            if (!Ctx.IsUsable())
            {
                return true;
            }
            const FVector Right = Ctx.Character->GetActorRightVector();
            const FVector At = Ctx.Character->GetActorLocation() + Right * 700.0;
            FVector Ground;
            if (!GroundAt(Ctx.World, At.X, At.Y, Ground))
            {
                Ctx.MarkInvalid(TEXT("No ground 700 uu right of the player for old_vs_new"));
                return true;
            }
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            APSLCharacter* Old = Ctx.World->SpawnActor<APSLCharacter>(APSLCharacter::StaticClass(), Ground, Ctx.Character->GetActorRotation(), Params);
            if (!Old)
            {
                Ctx.MarkInvalid(TEXT("Could not spawn the old T-Rex"));
                return true;
            }
            Shot->Other = Old;
            SetIntCvar(TEXT("psl.Dev.HeroTrex"), 0);
            Old->ApplySpecies(UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::TRexId, Old));
            SetIntCvar(TEXT("psl.Dev.HeroTrex"), 1);
            const USkeletalMesh* OldAsset = Old->GetMesh()->GetSkeletalMeshAsset();
            Ctx.Test->TestTrue(TEXT("Spawned mesh is /Game/Characters/Dino/SK_Trex"), OldAsset && OldAsset->GetPathName() == TEXT("/Game/Characters/Dino/SK_Trex.SK_Trex"));
            // One camera frames both bodies, which lie along the player's right axis.
            const FVector Centre = (Ctx.Character->GetActorLocation() + Ground) * 0.5;
            const FVector Side = Right * FrameDistance(700.0 + BodyLength(Ctx), 0.8);
            PlaceCamera(Ctx, *Shot, Centre, Centre, -Side, Side);
            return true;
        });
        QueueHold(Context, 1.0);
        AddPieStep(Context, TEXT("old_vs_new_shot"), 5.0f, [](FPSLPieCtx& Ctx)
        {
            if (Ctx.IsUsable())
            {
                RequestScreenshot(Ctx, TEXT("old_vs_new"));
            }
            return true;
        });
        QueueAwaitScreenshot(Context, TEXT("old_vs_new"));
        AddPieStep(Context, TEXT("RestoreView"), 2.0f, [Shot](FPSLPieCtx& Ctx)
        {
            Shot->RestoreView(Ctx);
            return true;
        });
    }
    // HeroVideo with the gameplay (pawn) camera at a fixed 30 fps step.
    AddPieStep(Context, TEXT("HeroVideo"), 600.0f, [S = MakeShared<FVideoState>(Guard)](FPSLPieCtx& Ctx)
    {
        return !Ctx.IsUsable() || S->Tick(Ctx);
    });
    AddPieStep(Context, TEXT("Cleanup"), 2.0f, [Shot, Guard](FPSLPieCtx& Ctx)
    {
        SetIntCvar(TEXT("psl.Dev.HeroTrex"), 0);
        SetPredatorSpawnCvar(1);
        Guard->Restore();
        Shot->RestoreView(Ctx);
        if (Ctx.HasObjects())
        {
            SendKey(Ctx, EKeys::W, IE_Released);
            SendKey(Ctx, EKeys::LeftShift, IE_Released);
            SendKey(Ctx, EKeys::LeftMouseButton, IE_Released);
            Ctx.PC->ResetIgnoreLookInput();
        }
        return true;
    });
    QueuePieEnd(Context);
    return true;
}

} // namespace

#endif
