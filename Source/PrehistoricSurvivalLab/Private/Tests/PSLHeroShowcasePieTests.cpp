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
#include "GameFramework/PlayerStart.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UObject/UObjectIterator.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"
#include "Misc/FileHelper.h"
#include "PSLDinoSpecies.h"

namespace
{
using namespace PSLPie;

constexpr const TCHAR* HeroMapPath = TEXT("/Game/Maps/L_LostValley");
constexpr const TCHAR* HeroMeshPackage = TEXT("/Game/Dev/ArtDir/Hero/SK_HeroTrex");
constexpr const TCHAR* HeroDir = TEXT("/Game/Dev/ArtDir/Hero/");

// Open-ground check: no other APSLCharacter / prey within 1500 uu of Spot and a capsule sweep of 3500 uu along Yaw is clear with ground
// found every 250 uu.
bool IsOpenSpot(FPSLPieCtx& Ctx, const FVector& Spot, double YawDeg)
{
    for (TActorIterator<APSLCharacter> It(Ctx.World); It; ++It)
    {
        if (*It != Ctx.Character.Get() && FVector::Dist2D(It->GetActorLocation(), Spot) < 1500.0)
        {
            return false;
        }
    }
    for (const APSLPreyCharacter* Prey : GetPrey(Ctx.World))
    {
        if (Prey && FVector::Dist2D(Prey->GetActorLocation(), Spot) < 1500.0)
        {
            return false;
        }
    }
    const FVector Dir = FRotator(0.0, YawDeg, 0.0).Vector();
    for (int32 Step = 1; Step <= 14; ++Step)
    {
        FVector Ground;
        const FVector P = Spot + Dir * (250.0 * Step);
        if (!GroundAt(Ctx.World, P.X, P.Y, Ground))
        {
            return false;
        }
    }
    const UCapsuleComponent* Capsule = Ctx.Character->GetCapsuleComponent();
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Ctx.Character.Get());
    FHitResult Hit;
    // Sweep ECC_Pawn at +100 uu so gentle slopes (ground is already +120 above the landscape) do not count as blockers.
    return !Ctx.World->SweepSingleByChannel(Hit, Spot + FVector(0.0, 0.0, 100.0), Spot + Dir * 3500.0 + FVector(0.0, 0.0, 100.0), FQuat::Identity, ECC_Pawn,
        FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params);
}

// Deterministic search: rings of 0..4000 uu (step 1000, 8 angles each) around the PlayerStart (else the player), 8 yaws per spot.
// Teleports the hero to the first open spot facing the clear direction. False when none exists.
bool MoveToOpenGround(FPSLPieCtx& Ctx)
{
    FVector Center = Ctx.Character->GetActorLocation();
    for (TActorIterator<APlayerStart> It(Ctx.World); It; ++It)
    {
        Center = It->GetActorLocation();
        break;
    }
    for (int32 Ring = 0; Ring <= 4; ++Ring)
    {
        for (int32 Angle = 0; Angle < (Ring == 0 ? 1 : 8); ++Angle)
        {
            const double A = FMath::DegreesToRadians(45.0 * Angle);
            FVector Ground;
            if (!GroundAt(Ctx.World, Center.X + 1000.0 * Ring * FMath::Cos(A), Center.Y + 1000.0 * Ring * FMath::Sin(A), Ground))
            {
                continue;
            }
            for (int32 Yaw = 0; Yaw < 8; ++Yaw)
            {
                if (IsOpenSpot(Ctx, Ground, 45.0 * Yaw))
                {
                    TeleportTo(Ctx, Ground);
                    FaceYawToward(Ctx, Ground + FRotator(0.0, 45.0 * Yaw, 0.0).Vector() * 1000.0);
                    Ctx.Test->AddInfo(FString::Printf(TEXT("Open stage at %s yaw %.0f"), *Ground.ToString(), 45.0 * Yaw));
                    return true;
                }
            }
        }
    }
    return false;
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
    if (T.TestNotNull(TEXT("DeathSequence set (A_HeroTrex_Death)"), Anim->DeathSequence.Get()))
    {
        T.TestTrue(TEXT("Death clip skeleton equals mesh skeleton"), Anim->DeathSequence->GetSkeleton() == Skeleton);
    }

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

// Per side, records every DEF-toe*/DEF-metatarsus* bone per frame. Report picks ONE planted bone per side (lowest mean Z over the
// cycle window), takes its stance frames (Z <= window-min Z + 3 uu) and uses the median horizontal world speed as the slide.
struct FFootTrack
{
    struct FFrame
    {
        double Time = 0.0;
        FVector Forward = FVector::ForwardVector;
        TArray<FVector> Pos;
    };
    TArray<FName> Bones[2];
    TArray<FFrame> Frames[2];

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
                }
            }
        }
    }

    void Sample(const USkeletalMeshComponent& Mesh, const FVector& Forward2D, double Now)
    {
        for (int32 Side = 0; Side < 2; ++Side)
        {
            if (Bones[Side].Num() == 0 || (Frames[Side].Num() > 0 && Now <= Frames[Side].Last().Time))
            {
                continue;
            }
            FFrame& F = Frames[Side].AddDefaulted_GetRef();
            F.Time = Now;
            F.Forward = Forward2D;
            for (const FName& Bone : Bones[Side])
            {
                F.Pos.Add(Mesh.GetBoneLocation(Bone, EBoneSpaces::WorldSpace));
            }
        }
    }

    // Window [End - BaseCycle / Rate, End]; asserts median stance slide / ground <= 0.25 per foot and logs the anim speed that would zero it.
    void Report(FPSLPieCtx& Ctx, const TCHAR* Label, double End, double BaseCycle, double Rate, double GroundSpeed)
    {
        const double Cycle = BaseCycle / FMath::Max(Rate, 0.01);
        const TCHAR* SideName[2] = {TEXT("L"), TEXT("R")};
        for (int32 Side = 0; Side < 2; ++Side)
        {
            const TArray<FFrame>& Fr = Frames[Side];
            int32 First = 0;
            while (First < Fr.Num() && Fr[First].Time < End - Cycle)
            {
                ++First;
            }
            const int32 NumBones = Bones[Side].Num();
            int32 Planted = INDEX_NONE;
            double PlantedMean = TNumericLimits<double>::Max();
            for (int32 B = 0; B < NumBones && First < Fr.Num(); ++B)
            {
                double Sum = 0.0;
                for (int32 I = First; I < Fr.Num(); ++I)
                {
                    Sum += Fr[I].Pos[B].Z;
                }
                const double Mean = Sum / (Fr.Num() - First);
                if (Mean < PlantedMean)
                {
                    PlantedMean = Mean;
                    Planted = B;
                }
            }
            TArray<double> Slide;
            double ForwardSum = 0.0;
            if (Planted != INDEX_NONE)
            {
                double MinZ = TNumericLimits<double>::Max();
                for (int32 I = First; I < Fr.Num(); ++I)
                {
                    MinZ = FMath::Min(MinZ, Fr[I].Pos[Planted].Z);
                }
                for (int32 I = FMath::Max(First, 1); I < Fr.Num(); ++I)
                {
                    if (Fr[I].Pos[Planted].Z <= MinZ + 3.0)
                    {
                        const FVector Vel = (Fr[I].Pos[Planted] - Fr[I - 1].Pos[Planted]) / FMath::Max(Fr[I].Time - Fr[I - 1].Time, KINDA_SMALL_NUMBER);
                        Slide.Add(FVector(Vel.X, Vel.Y, 0.0).Size());
                        ForwardSum += FVector::DotProduct(FVector(Vel.X, Vel.Y, 0.0), Fr[I].Forward);
                    }
                }
            }
            Slide.Sort();
            const int32 N = Slide.Num();
            const double Median = N == 0 ? 0.0 : (N % 2 ? Slide[N / 2] : 0.5 * (Slide[N / 2 - 1] + Slide[N / 2]));
            const double VForward = N == 0 ? 0.0 : ForwardSum / N;
            const double Ratio = GroundSpeed > 1.0 && N > 0 ? Median / GroundSpeed : 1.0;
            const double Suggested = (GroundSpeed - VForward) / FMath::Max(Rate, 0.01);
            const FString BoneName = Planted != INDEX_NONE ? Bones[Side][Planted].ToString() : FString(TEXT("none"));
            const FString Line = FString::Printf(
                TEXT("{\"step\":\"%s_foot_slide\",\"foot\":\"%s\",\"bone\":\"%s\",\"ground_speed\":%.1f,\"play_rate\":%.3f,\"cycle\":%.2f,\"stance_frames\":%d,\"median_slide\":%.2f,\"forward_component\":%.2f,\"ratio\":%.4f,\"suggested_anim_speed\":%.1f}"),
                Label, SideName[Side], *BoneName, GroundSpeed, Rate, Cycle, N, Median, VForward, Ratio, Suggested);
            Ctx.Measurements.Add(Line);
            Ctx.Test->AddInfo(Line);
            Ctx.Test->TestTrue(FString::Printf(TEXT("%s foot %s has >= 5 stance frames (%d, bone %s)"), Label, SideName[Side], N, *BoneName), N >= 5);
            Ctx.Test->TestTrue(FString::Printf(TEXT("%s foot %s slide ratio %.3f <= 0.25 (median %.1f of %.1f uu/s)"), Label, SideName[Side], Ratio, Median, GroundSpeed), Ratio <= 0.25);
            Frames[Side].Reset();
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
        return A && A->WalkAnimSpeed > 0.0f ? FMath::Clamp(V / A->WalkAnimSpeed, 0.5, 6.0) : 1.0;
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
        Feet.Sample(*Mesh, Ctx.Character->GetActorForwardVector().GetSafeNormal2D(), Now);
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
    // Bite timing: DEF-jaw rotation relative to its parent, sampled every tick after the press, against the idle pose before it.
    const FName JawName = TEXT("DEF-jaw");
    FName JawBone = NAME_None;
    FQuat IdleJaw = FQuat::Identity;
    double HitTime = -1.0;
    TArray<TPair<double, double>> JawSamples;

    FQuat JawLocal(const USkeletalMeshComponent& Mesh) const
    {
        return Mesh.GetBoneQuaternion(Mesh.GetParentBone(JawBone), EBoneSpaces::ComponentSpace).Inverse() * Mesh.GetBoneQuaternion(JawBone, EBoneSpaces::ComponentSpace);
    }
    double JawDeltaDeg(const USkeletalMeshComponent& Mesh) const
    {
        return FMath::RadiansToDegrees(IdleJaw.AngularDistance(JawLocal(Mesh)));
    }

    // Hero contact timing (r7): the hit lands ~ (1.567 - 0.6) / 1.5 s after the press with the jaw open >= 40 deg just before it.
    void AssertBiteTiming(FPSLPieCtx& Ctx) const
    {
        FAutomationTestBase& T = *Ctx.Test;
        const float ExpectedDelay = (1.567f - 0.6f) / 1.5f;
        T.TestTrue(FString::Printf(TEXT("BiteHitDelay %.4f equals (1.567-0.6)/1.5 = %.4f"), Ctx.Character->BiteHitDelay, ExpectedDelay),
            FMath::IsNearlyEqual(Ctx.Character->BiteHitDelay, ExpectedDelay, 0.001f));
        T.TestTrue(FString::Printf(TEXT("Bite hit lands %.3f s after the press (0.645 +- 0.1)"), HitTime), HitTime >= 0.0 && FMath::Abs(HitTime - 0.645) <= 0.1);
        double MaxDelta = 0.0;
        for (const TPair<double, double>& Sample : JawSamples)
        {
            if (Sample.Key >= HitTime - 0.15 && Sample.Key <= HitTime)
            {
                MaxDelta = FMath::Max(MaxDelta, Sample.Value);
            }
        }
        T.TestTrue(FString::Printf(TEXT("DEF-jaw local rotation delta vs idle >= 40 deg within [hit-0.15, hit] (max %.1f deg, hit %.3f s)"), MaxDelta, HitTime), MaxDelta >= 40.0);
        const float Dropped = PreyBefore - Prey->GetStatsComponent()->GetHealth();
        T.TestTrue(FString::Printf(TEXT("Prey health dropped by BiteDamage 25 (%.1f)"), Dropped), FMath::IsNearlyEqual(Dropped, 25.0f, 0.5f));
        T.TestTrue(FString::Printf(TEXT("LastBiteHitCount >= 1 (%d)"), Ctx.Character->LastBiteHitCount), Ctx.Character->LastBiteHitCount >= 1);
    }

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
            const USkeletalMeshComponent* Mesh = Ctx.Character->GetMesh();
            if (Mesh->GetBoneIndex(JawName) != INDEX_NONE && Mesh->GetParentBone(JawName) != NAME_None)
            {
                JawBone = JawName;
                IdleJaw = JawLocal(*Mesh);
            }
            Ctx.Test->TestTrue(TEXT("DEF-jaw bone (with parent) exists on the hero"), JawBone != NAME_None);
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
        const double SincePress = Now - PressTime;
        if (JawBone != NAME_None)
        {
            JawSamples.Add({SincePress, JawDeltaDeg(*Ctx.Character->GetMesh())});
        }
        if (HitTime < 0.0 && Prey->GetStatsComponent()->GetHealth() < PreyBefore)
        {
            HitTime = SincePress;
        }
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
            AssertBiteTiming(Ctx);
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
    UseCamera(Ctx, S, Spot, LookAt);
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

// Head bone location, else the top of the bounds.
FVector HeadPoint(const USkeletalMeshComponent* Mesh)
{
    const FName Head(TEXT("DEF-head"));
    if (Mesh->GetBoneIndex(Head) != INDEX_NONE)
    {
        return Mesh->GetBoneLocation(Head, EBoneSpaces::WorldSpace);
    }
    return Mesh->Bounds.Origin + FVector(0.0, 0.0, Mesh->Bounds.BoxExtent.Z);
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
    TSharedRef<FShotState> Shot;
    int32 Phase = -1;
    double PhaseStart = 0.0;
    double TurnFrom = 0.0;
    FVector PhaseOrigin = FVector::ZeroVector;
    double ActorYawAtOrbit = 0.0;
    bool bStageSet = false;
    bool bPreyPlaced = false;
    bool bSawAttack = false;
    FString Dir;
    TArray<TPair<int32, double>> Frames;

    FVideoState(TSharedRef<FTimestepGuard> InGuard, TSharedRef<FShotState> InShot) : Guard(MoveTemp(InGuard)), Shot(MoveTemp(InShot)) {}

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
        Shot->RestoreView(Ctx);
    }

    // Logs the 2D displacement of the hero over the phase that just ended and returns it.
    double EndPhase(FPSLPieCtx& Ctx, const TCHAR* Name)
    {
        const double Moved = FVector::Dist2D(Ctx.Character->GetActorLocation(), PhaseOrigin);
        const FString Line = FString::Printf(TEXT("{\"step\":\"video_%s\",\"displacement_2d\":%.1f}"), Name, Moved);
        Ctx.Measurements.Add(Line);
        Ctx.Test->AddInfo(Line);
        return Moved;
    }

    bool Tick(FPSLPieCtx& Ctx)
    {
        const double Now = Ctx.World->GetTimeSeconds();
        if (Phase < 0)
        {
            Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("PIE"), TEXT("HeroVideo"));
            // Re-check the open stage (the earlier shots may have moved nothing, but the old T-Rex/prey may have wandered in).
            if (!IsOpenSpot(Ctx, Ctx.Character->GetActorLocation(), Ctx.Character->GetActorRotation().Yaw) && !MoveToOpenGround(Ctx))
            {
                Ctx.MarkInvalid(TEXT("No open ground found for HeroVideo"));
                return true;
            }
            Ctx.PC->SetControlRotation(FRotator(0.0, Ctx.Character->GetActorRotation().Yaw, 0.0));
            Guard->Enter();
            PhaseStart = Now;
            PhaseOrigin = Ctx.Character->GetActorLocation();
            Phase = 0;
        }
        if (FApp::CanEverRender())
        {
            if (CapturePieViewport(Ctx, TEXT("HeroVideo"), FPaths::Combine(Dir, FString::Printf(TEXT("frame_%04d.png"), Frames.Num())), Frames.Num() == 0))
            {
                Frames.Add({Frames.Num(), Now});
            }
        }
        const double In = Now - PhaseStart;
        auto Next = [&](int32 P) { Phase = P; PhaseStart = Now; };
        switch (Phase)
        {
        case 0: // idle 1 s
            if (In >= 1.0) { SendKey(Ctx, EKeys::W, IE_Pressed); PhaseOrigin = Ctx.Character->GetActorLocation(); Next(1); }
            break;
        case 1: // W 3 s
            if (In >= 3.0)
            {
                const double Moved = EndPhase(Ctx, TEXT("walk"));
                Ctx.Test->TestTrue(FString::Printf(TEXT("HeroVideo walk 3 s travels >= 600 uu (%.0f)"), Moved), Moved >= 600.0);
                SendKey(Ctx, EKeys::LeftShift, IE_Pressed);
                PhaseOrigin = Ctx.Character->GetActorLocation();
                Next(2);
            }
            break;
        case 2: // W+Shift 3 s
            if (In >= 3.0)
            {
                const double Moved = EndPhase(Ctx, TEXT("run"));
                Ctx.Test->TestTrue(FString::Printf(TEXT("HeroVideo run 3 s travels >= 1200 uu (%.0f)"), Moved), Moved >= 1200.0);
                TurnFrom = Ctx.PC->GetControlRotation().Yaw;
                PhaseOrigin = Ctx.Character->GetActorLocation();
                Next(3);
            }
            break;
        case 3: // turn +180 over 2 s while running
            Ctx.PC->SetControlRotation(FRotator(0.0, TurnFrom + 180.0 * FMath::Clamp(In / 2.0, 0.0, 1.0), 0.0));
            if (In >= 2.0)
            {
                EndPhase(Ctx, TEXT("turn"));
                const double Diff = FMath::Abs(FRotator::NormalizeAxis(Ctx.Character->GetActorRotation().Yaw - (TurnFrom + 180.0)));
                Ctx.Test->TestTrue(FString::Printf(TEXT("HeroVideo actor yaw within 30 deg of the 180 deg turn target (off by %.1f)"), Diff), Diff <= 30.0);
                SendKey(Ctx, EKeys::LeftShift, IE_Released);
                SendKey(Ctx, EKeys::W, IE_Released);
                Next(4);
            }
            break;
        case 4: // stopped 1.5 s: the view switches to the side camera at the start; after the actor settles the camera is re-aimed and the prey is put in front
            if (!bStageSet)
            {
                SideShot(Ctx, *Shot);
                bStageSet = true;
            }
            if (In >= 0.5 && !bPreyPlaced)
            {
                bPreyPlaced = true;
                const FVector Front = Ctx.Character->GetActorLocation() + Ctx.Character->GetActorForwardVector() * 220.0;
                FVector Ground;
                if (!GroundAt(Ctx.World, Front.X, Front.Y, Ground))
                {
                    // Same height the hero stands at, minus the capsule difference: the prey drops onto the terrain.
                    Ground = Front - FVector(0.0, 0.0, 60.0);
                    Ctx.Test->AddInfo(TEXT("HeroVideo bite: no landscape hit 220 uu ahead, prey placed at hero height"));
                }
                APSLPreyCharacter* BitePrey = nullptr;
                for (APSLPreyCharacter* Candidate : GetPrey(Ctx.World))
                {
                    if (Candidate && !Candidate->GetStatsComponent()->IsDead())
                    {
                        BitePrey = Candidate;
                        break;
                    }
                }
                if (BitePrey)
                {
                    Shot->PreyHome = BitePrey->GetActorLocation();
                    BitePrey->SetActorLocation(Ground, false, nullptr, ETeleportType::TeleportPhysics);
                }
                else
                {
                    // Earlier steps (bite test, predator) left no living prey: spawn one for the shot.
                    FActorSpawnParameters SpawnParams;
                    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                    BitePrey = Ctx.World->SpawnActor<APSLPreyCharacter>(APSLPreyCharacter::StaticClass(), Ground, FRotator::ZeroRotator, SpawnParams);
                    if (!BitePrey)
                    {
                        Ctx.MarkInvalid(TEXT("HeroVideo bite: no living prey and spawning one failed"));
                        Finish(Ctx);
                        return true;
                    }
                    Shot->bPreySpawned = true;
                    Ctx.Test->AddInfo(TEXT("HeroVideo bite: no living prey in the world, spawned one"));
                }
                // The bite centre is 220 uu ahead of the actor, radius 120: the prey sits inside it, in front of the open jaw.
                Shot->Prey = BitePrey;
                Shot->bPreyMoved = true;
                HoldPreyStill(Ctx, *BitePrey);
                ActorYawAtOrbit = Ctx.Character->GetActorRotation().Yaw;
                SideShot(Ctx, *Shot);
            }
            if (bPreyPlaced && Shot->Prey.IsValid())
            {
                HoldPreyStill(Ctx, *Shot->Prey);
            }
            if (In >= 1.5)
            {
                const FVector CamLoc = Shot->Cam.IsValid() ? Shot->Cam->GetActorLocation() : Ctx.Character->FindComponentByClass<UCameraComponent>()->GetComponentLocation();
                const FVector ToCam = (CamLoc - Ctx.Character->GetActorLocation()).GetSafeNormal2D();
                const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(ToCam, -Ctx.Character->GetActorForwardVector().GetSafeNormal2D()), -1.0, 1.0)));
                const double YawDrift = FMath::Abs(FRotator::NormalizeAxis(Ctx.Character->GetActorRotation().Yaw - ActorYawAtOrbit));
                Ctx.Test->TestTrue(FString::Printf(TEXT("Actor yaw did not follow the camera orbit (drift %.1f deg <= 5)"), YawDrift), YawDrift <= 5.0);
                const FString Line = FString::Printf(TEXT("{\"step\":\"video_bite_camera\",\"camera_vs_rear_axis_deg\":%.1f}"), Angle);
                Ctx.Measurements.Add(Line);
                Ctx.Test->AddInfo(Line);
                Ctx.Test->TestTrue(FString::Printf(TEXT("Bite camera is >= 60 deg off the hero rear axis (%.1f)"), Angle), Angle >= 60.0);
                Ctx.Test->TestTrue(TEXT("HeroVideo bite: TryBite returns true"), Ctx.Character->TryBite());
                bSawAttack = PlayerActiveAction(Ctx) == EPSLDinoAction::Attack;
                Next(5);
            }
            break;
        default: // bite + 2.0 s
            if (Shot->Prey.IsValid())
            {
                HoldPreyStill(Ctx, *Shot->Prey);
            }
            bSawAttack = bSawAttack || PlayerActiveAction(Ctx) == EPSLDinoAction::Attack;
            if (In >= 2.0)
            {
                Ctx.Test->TestTrue(TEXT("HeroVideo bite: attack action played during the bite phase"), bSawAttack);
                Finish(Ctx);
                return true;
            }
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
    // Old-species regression: with the cvar at 0 the built-in T-Rex keeps BiteHitDelay 0.35 and the attack clip window (start 0, end 0 = full clip).
    AddPieStep(Context, TEXT("OldSpeciesBiteRegression"), 5.0f, [](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSLCharacter* Old = Ctx.World->SpawnActor<APSLCharacter>(APSLCharacter::StaticClass(), Ctx.Character->GetActorLocation() + FVector(0.0, 0.0, 2000.0), FRotator::ZeroRotator, Params);
        if (!Ctx.Test->TestNotNull(TEXT("Old-species regression: spawned APSLCharacter"), Old))
        {
            return true;
        }
        SetIntCvar(TEXT("psl.Dev.HeroTrex"), 0);
        Old->ApplySpecies(UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::TRexId, Old));
        SetIntCvar(TEXT("psl.Dev.HeroTrex"), 1);
        Ctx.Test->TestEqual(TEXT("Old T-Rex BiteHitDelay stays 0.35"), Old->BiteHitDelay, 0.35f);
        const UPSLDinoAnimInstance* OldAnim = Cast<UPSLDinoAnimInstance>(Old->GetMesh()->GetAnimInstance());
        if (Ctx.Test->TestNotNull(TEXT("Old T-Rex anim instance"), OldAnim))
        {
            Ctx.Test->TestEqual(TEXT("Old T-Rex AttackStartTime is 0"), OldAnim->AttackStartTime, 0.0f);
            Ctx.Test->TestEqual(TEXT("Old T-Rex AttackEndTime is 0 (full clip)"), OldAnim->AttackEndTime, 0.0f);
        }
        Old->Destroy();
        return true;
    });
    // Stills and video need a renderer; under -nullrhi only the video script (without capture) runs.
    const TSharedRef<FShotState> Shot = MakeShared<FShotState>();
    const TSharedRef<FTimestepGuard> Guard = MakeShared<FTimestepGuard>();
    if (FApp::CanEverRender())
    {
        QueueHold(Context, 1.0);
        // hero_attack_side keeps the prey next to the hero; the open-ground stage is used for every later shot and the video.
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
            return Now - Start >= 0.45;
        });
        QueueCapture(Context, TEXT("hero_attack_side"));
        // bite_seq_0..3: PIE stills at press + 0.30/0.50/0.645/0.85 s with a prey held in front (side camera from hero_attack_side stays).
        QueueHold(Context, 1.2);
        AddPieStep(Context, TEXT("bite_seq"), 10.0f, [Shot, Start = -1.0, Next = 0](FPSLPieCtx& Ctx) mutable
        {
            if (!Ctx.IsUsable())
            {
                return true;
            }
            const double Now = Ctx.World->GetTimeSeconds();
            for (APSLPreyCharacter* Prey : GetPrey(Ctx.World))
            {
                if (Prey && !Prey->GetStatsComponent()->IsDead())
                {
                    HoldPreyStill(Ctx, *Prey);
                    break;
                }
            }
            if (Start < 0.0)
            {
                SideShot(Ctx, *Shot);
                Ctx.Test->TestTrue(TEXT("TryBite for bite_seq"), Ctx.Character->TryBite());
                Start = Now;
            }
            static const double Offsets[4] = {0.30, 0.50, 0.645, 0.85};
            if (Next < 4 && Now - Start >= Offsets[Next])
            {
                const FString Label = FString::Printf(TEXT("bite_seq_%d"), Next);
                CapturePieViewport(Ctx, Label, ShotPath(Ctx, Label), true);
                Ctx.Test->AddInfo(FString::Printf(TEXT("%s captured %.3f s after the press (target %.3f)"), *Label, Now - Start, Offsets[Next]));
                ++Next;
            }
            return Next >= 4;
        });
        QueueHold(Context, 1.5);
        AddPieStep(Context, TEXT("OpenGroundStage"), 5.0f, [](FPSLPieCtx& Ctx)
        {
            if (Ctx.IsUsable() && !MoveToOpenGround(Ctx))
            {
                Ctx.MarkInvalid(TEXT("No open ground (no character within 1500 uu, 3500 uu clear sweep with ground every 250 uu) found in L_LostValley"));
            }
            return true;
        });
        QueueHold(Context, 1.0);
        AddPieStep(Context, TEXT("hero_side_full"), 5.0f, [Shot](FPSLPieCtx& Ctx)
        {
            if (Ctx.IsUsable())
            {
                SideShot(Ctx, *Shot);
            }
            return true;
        });
        QueueCapture(Context, TEXT("hero_side_full"));
        AddPieStep(Context, TEXT("hero_head_closeup"), 5.0f, [Shot](FPSLPieCtx& Ctx)
        {
            if (Ctx.IsUsable())
            {
                const USkeletalMeshComponent* Mesh = Ctx.Character->GetMesh();
                const FVector Head = Mesh->GetBoneLocation(TEXT("DEF-head"), EBoneSpaces::WorldSpace);
                const FVector Fwd = Ctx.Character->GetActorForwardVector() * FMath::Cos(FMath::DegreesToRadians(40.0));
                const FVector Side = Ctx.Character->GetActorRightVector() * FMath::Sin(FMath::DegreesToRadians(40.0));
                PlaceCamera(Ctx, *Shot, Head, Head, (Fwd - Side) * 200.0, (Fwd + Side) * 200.0);
            }
            return true;
        });
        QueueCapture(Context, TEXT("hero_head_closeup"));
        // Walking third-person shot through the pawn camera.
        AddPieStep(Context, TEXT("hero_thirdperson_walk"), 10.0f, [Shot, Start = -1.0](FPSLPieCtx& Ctx) mutable
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
            return Now - Start >= 1.5;
        });
        QueueCapture(Context, TEXT("hero_thirdperson"));
        AddPieStep(Context, TEXT("ReleaseW"), 2.0f, [](FPSLPieCtx& Ctx)
        {
            SendKey(Ctx, EKeys::W, IE_Released);
            return true;
        });
        QueueHold(Context, 1.0);
        // Old (SK_Trex) T-Rex on the verified clear corridor (hero +-Forward*800), one camera on the hero's Right axis framing both.
        // The first configuration (old side, camera side, camera raise) with zero instanced-foliage occluders is used.
        AddPieStep(Context, TEXT("old_vs_new"), 5.0f, [Shot](FPSLPieCtx& Ctx)
        {
            if (!Ctx.IsUsable())
            {
                return true;
            }
            const FVector HeroLoc = Ctx.Character->GetActorLocation();
            const FVector Forward = Ctx.Character->GetActorForwardVector();
            const FVector Right = Ctx.Character->GetActorRightVector();
            FVector Ground;
            if (!GroundAt(Ctx.World, HeroLoc.X + Forward.X * 800.0, HeroLoc.Y + Forward.Y * 800.0, Ground))
            {
                Ctx.MarkInvalid(TEXT("No ground 800 uu in front of the player for old_vs_new"));
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

            const USkeletalMeshComponent* NewMesh = Ctx.Character->GetMesh();
            const USkeletalMeshComponent* OldMesh = Old->GetMesh();
            const double Dist = FrameDistance(800.0 + BodyLength(Ctx), 0.8);
            FString Tried;
            for (const double OldSide : {1.0, -1.0})
            {
                if (!GroundAt(Ctx.World, HeroLoc.X + Forward.X * 800.0 * OldSide, HeroLoc.Y + Forward.Y * 800.0 * OldSide, Ground))
                {
                    Tried += FString::Printf(TEXT("[old %+.0f: no ground] "), OldSide);
                    continue;
                }
                Old->SetActorLocation(Ground, false, nullptr, ETeleportType::TeleportPhysics);
                const FVector Centre = (HeroLoc + Ground) * 0.5;
                const FVector Targets[4] = {NewMesh->Bounds.Origin, HeadPoint(NewMesh), OldMesh->Bounds.Origin, HeadPoint(OldMesh)};
                for (const double CamSide : {-1.0, 1.0})
                {
                    for (const double Raise : {0.0, 200.0, 400.0})
                    {
                        const FVector Cam = Centre + Right * (CamSide * Dist) + FVector(0.0, 0.0, Raise);
                        int32 Counts[4];
                        int32 Total = 0;
                        for (int32 I = 0; I < 4; ++I)
                        {
                            Counts[I] = OccluderCount(Ctx.World, Cam, Targets[I], Ctx.Character.Get(), Old);
                            Total += Counts[I];
                        }
                        const FString Config = FString::Printf(TEXT("old Forward*%+.0f, camera Right*%+.0f, raise %.0f: occluders new-body %d new-head %d old-body %d old-head %d"),
                            800.0 * OldSide, CamSide, Raise, Counts[0], Counts[1], Counts[2], Counts[3]);
                        if (Total == 0)
                        {
                            Ctx.Test->AddInfo(FString::Printf(TEXT("old_vs_new chose %s"), *Config));
                            Ctx.Test->TestEqual(TEXT("old_vs_new: instanced-foliage occluders on every camera-to-body segment"), Total, 0);
                            UseCamera(Ctx, *Shot, Cam, Centre);
                            return true;
                        }
                        Tried += FString::Printf(TEXT("[%s] "), *Config);
                    }
                }
            }
            Ctx.MarkInvalid(FString::Printf(TEXT("old_vs_new: no foliage-free configuration: %s"), *Tried));
            return true;
        });
        QueueHold(Context, 1.0);
        AddPieStep(Context, TEXT("old_vs_new_project"), 5.0f, [Shot](FPSLPieCtx& Ctx)
        {
            if (!Ctx.IsUsable() || !Shot->Other.IsValid())
            {
                return true;
            }
            int32 W = 0, H = 0;
            Ctx.PC->GetViewportSize(W, H);
            FVector2D New, Old;
            const bool bNew = Ctx.PC->ProjectWorldLocationToScreen(Ctx.Character->GetMesh()->Bounds.Origin, New);
            const bool bOld = Ctx.PC->ProjectWorldLocationToScreen(Shot->Other->GetMesh()->Bounds.Origin, Old);
            const bool bInside = bNew && bOld && New.X >= 0.0 && New.X <= W && New.Y >= 0.0 && New.Y <= H && Old.X >= 0.0 && Old.X <= W && Old.Y >= 0.0 && Old.Y <= H;
            const double Apart = FMath::Abs(New.X - Old.X) / FMath::Max(W, 1);
            Ctx.Test->AddInfo(FString::Printf(TEXT("old_vs_new projection: new (%.0f,%.0f) old (%.0f,%.0f) viewport %dx%d apart %.2f"), New.X, New.Y, Old.X, Old.Y, W, H, Apart));
            Ctx.Test->TestTrue(TEXT("old_vs_new: both dinos project inside the viewport"), bInside);
            Ctx.Test->TestTrue(FString::Printf(TEXT("old_vs_new: dinos at least 25%% of the viewport width apart (%.2f)"), Apart), Apart >= 0.25);
            return true;
        });
        QueueCapture(Context, TEXT("old_vs_new"));
        AddPieStep(Context, TEXT("RestoreView"), 2.0f, [Shot](FPSLPieCtx& Ctx)
        {
            Shot->RestoreView(Ctx);
            return true;
        });
    }
    // HeroVideo: pawn camera until the stop phase, then the temporary side camera (restored by Finish and Cleanup) at a fixed 30 fps step.
    AddPieStep(Context, TEXT("HeroVideo"), 600.0f, [S = MakeShared<FVideoState>(Guard, Shot)](FPSLPieCtx& Ctx)
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
