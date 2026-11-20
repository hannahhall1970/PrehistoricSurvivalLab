#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "PSLPieTestSupport.h"
#include "PSLDinoSpecies.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

// PSL.Dev.HeroTrex.Death: a spawned, non-player hero T-Rex dies (HandleDeath, same path as the player) and the hero Death clip
// plays once, holds its last frame, rests on the floor and never returns to idle. A non-player is used because the player's
// death pauses the game after APSLGameMode::GameOverDelay, which would freeze the world exactly when the clip ends.
namespace
{
using namespace PSLPie;

// Unity builds merge anonymous namespaces: names here must not collide with PSLHeroShowcasePieTests.cpp.
constexpr const TCHAR* DeathTestHeroMeshPackage = TEXT("/Game/Dev/ArtDir/Hero/SK_HeroTrex");
constexpr const TCHAR* DeathTestHeroMeshPath = TEXT("/Game/Dev/ArtDir/Hero/SK_HeroTrex.SK_HeroTrex");
constexpr const TCHAR* DeathTestHeroDeathPath = TEXT("/Game/Dev/ArtDir/Hero/A_HeroTrex_Death.A_HeroTrex_Death");
// death_0 is captured in the Kill step one tick before ApplyDamage (standing pose): a synchronous 1080p ReadPixels between the kill
// and the first anim update would otherwise be counted as Death-start latency (r7 heroplay-run4: 0.206 s). death_1..4 follow at
// these times after the kill.
constexpr double DeathShotTimes[4] = {0.5, 1.0, 1.6, 3.0};

struct FHeroDeathState
{
    TWeakObjectPtr<APSLCharacter> Hero;
    TSharedRef<FShotState> Shot = MakeShared<FShotState>();
    double FloorZ = 0.0;
    double StandPelvisAbove = 0.0;
    FVector CapsuleStart = FVector::ZeroVector;
    double IdleStart = -1.0;
    double DeathWorldTime = -1.0;
    double FirstDeathAt = -1.0;
    bool bLeftDeath = false;
    FString LeftDeathTo;
    double MinWeight = TNumericLimits<double>::Max();
    double MaxWeight = TNumericLimits<double>::Lowest();
    bool bHave22 = false;
    double PelvisAbove22 = 0.0;
    double MinHoldZ = TNumericLimits<double>::Max();
    double MaxHoldZ = TNumericLimits<double>::Lowest();
    bool bHave30 = false;
    double Lowest30 = 0.0;
    double MaxCapsuleMove = 0.0;
    bool bShot0 = false;
    int32 NextShot = 0;
    int32 Ticks = 0;
};

double PelvisZ(const USkeletalMeshComponent* Mesh)
{
    return Mesh->GetBoneLocation(TEXT("DEF-pelvis"), EBoneSpaces::WorldSpace).Z;
}

// Lowest deform (DEF-*) bone: the skinned body; root/IK helpers can sit below the visible mesh.
double LowestBoneZ(const USkeletalMeshComponent* Mesh)
{
    double Lowest = TNumericLimits<double>::Max();
    for (int32 Bone = 0; Bone < Mesh->GetNumBones(); ++Bone)
    {
        const FName Name = Mesh->GetBoneName(Bone);
        if (Name.ToString().StartsWith(TEXT("DEF-")))
        {
            Lowest = FMath::Min(Lowest, Mesh->GetBoneLocation(Name, EBoneSpaces::WorldSpace).Z);
        }
    }
    return Lowest;
}

const TCHAR* ActionName(EPSLDinoAction Action)
{
    return Action == EPSLDinoAction::Death ? TEXT("Death") : Action == EPSLDinoAction::Attack ? TEXT("Attack") : TEXT("None");
}

// Static floor below (X, Y) (WorldStatic only: prey/pawn capsules are ignored); true when it is within 300 uu of RefZ.
bool DeathTestFloorAt(FPSLPieCtx& Ctx, const FVector& XY, double RefZ, double& OutZ)
{
    FHitResult Hit;
    if (Ctx.World->LineTraceSingleByObjectType(Hit, FVector(XY.X, XY.Y, RefZ + 3000.0), FVector(XY.X, XY.Y, RefZ - 3000.0),
            FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(TEXT("PSLHeroDeathFloor"), false))
        && FMath::Abs(Hit.ImpactPoint.Z - RefZ) <= 300.0)
    {
        OutZ = Hit.ImpactPoint.Z;
        return true;
    }
    return false;
}

// Spawns the hero (cvar psl.Dev.HeroTrex is 1) on the floor in front of the player, checks its assets, sets the side camera.
bool SpawnDeathHero(FPSLPieCtx& Ctx, FHeroDeathState& S)
{
    FAutomationTestBase& T = *Ctx.Test;
    const FVector PlayerLoc = Ctx.Character->GetActorLocation();
    const double PlayerFloor = PlayerLoc.Z - Ctx.Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Forward = Ctx.Character->GetActorForwardVector().GetSafeNormal2D();
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSLCharacter* Hero = Ctx.World->SpawnActor<APSLCharacter>(APSLCharacter::StaticClass(), PlayerLoc + FVector(0.0, 0.0, 3000.0), Ctx.Character->GetActorRotation(), Params);
    if (!T.TestNotNull(TEXT("Spawned non-player hero APSLCharacter"), Hero))
    {
        return false;
    }
    S.Hero = Hero;
    Hero->ApplySpecies(UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::TRexId, Hero));
    T.TestNull(TEXT("Spawned hero has no controller"), Hero->GetController());

    bool bFloor = false;
    for (const double Distance : {1500.0, 1100.0, 800.0})
    {
        const FVector XY = PlayerLoc + Forward * Distance;
        if (DeathTestFloorAt(Ctx, XY, PlayerFloor, S.FloorZ))
        {
            const double Half = Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            Hero->SetActorLocation(FVector(XY.X, XY.Y, S.FloorZ + Half), false, nullptr, ETeleportType::TeleportPhysics);
            T.AddInfo(FString::Printf(TEXT("Death hero placed %.0f uu in front of the player, floor Z %.1f"), Distance, S.FloorZ));
            bFloor = true;
            break;
        }
    }
    if (!bFloor)
    {
        Ctx.MarkInvalid(TEXT("No arena floor 800-1500 uu in front of the player for the death hero"));
        return false;
    }

    USkeletalMeshComponent* Mesh = Hero->GetMesh();
    Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    const USkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
    T.TestTrue(TEXT("Death hero mesh is SK_HeroTrex"), Asset && Asset->GetPathName() == DeathTestHeroMeshPath);
    const UPSLDinoAnimInstance* Anim = Cast<UPSLDinoAnimInstance>(Mesh->GetAnimInstance());
    if (!T.TestNotNull(TEXT("Death hero anim instance"), Anim)
        || !T.TestTrue(TEXT("Death hero DeathSequence is A_HeroTrex_Death"), Anim->DeathSequence && Anim->DeathSequence->GetPathName() == DeathTestHeroDeathPath)
        || !T.TestTrue(TEXT("DEF-pelvis bone exists"), Mesh->GetBoneIndex(TEXT("DEF-pelvis")) != INDEX_NONE))
    {
        return false;
    }

    if (FApp::CanEverRender())
    {
        const FVector Extent = Mesh->Bounds.BoxExtent;
        const double BodyLen = 2.0 * FMath::Max(Extent.X, Extent.Y);
        const FVector Centre = Mesh->Bounds.Origin;
        UseCamera(Ctx, *S.Shot, Centre + Hero->GetActorRightVector() * FrameDistance(BodyLen, 0.6) + FVector(0.0, 0.0, 100.0), Centre);
    }
    return true;
}

// Per-tick sampling after the kill; returns true once 4.0 s of world time have passed since the kill, then asserts.
bool TickDeath(FPSLPieCtx& Ctx, FHeroDeathState& S)
{
    FAutomationTestBase& T = *Ctx.Test;
    APSLCharacter* Hero = S.Hero.Get();
    if (!Hero)
    {
        Ctx.MarkInvalid(TEXT("Death hero was destroyed during the death sequence"));
        return true;
    }
    USkeletalMeshComponent* Mesh = Hero->GetMesh();
    const UPSLDinoAnimInstance* Anim = Cast<UPSLDinoAnimInstance>(Mesh->GetAnimInstance());
    if (!Anim)
    {
        Ctx.MarkInvalid(TEXT("Death hero lost its anim instance"));
        return true;
    }
    const double Now = Ctx.World->GetTimeSeconds();
    const double Since = Now - S.DeathWorldTime;
    const EPSLDinoAction Action = Anim->GetActiveAction();
    const double Weight = Anim->GetActionWeight();
    const double Pelvis = PelvisZ(Mesh);
    const double Lowest = LowestBoneZ(Mesh);
    S.MaxCapsuleMove = FMath::Max(S.MaxCapsuleMove, FVector::Dist(Hero->GetActorLocation(), S.CapsuleStart));
    ++S.Ticks;
    if ((S.Ticks % 10) == 1)
    {
        T.AddInfo(FString::Printf(TEXT("{\"step\":\"death_sample\",\"t\":%.3f,\"action\":\"%s\",\"weight\":%.3f,\"pelvis_above_floor\":%.1f,\"lowest_bone_above_floor\":%.1f}"),
            Since, ActionName(Action), Weight, Pelvis - S.FloorZ, Lowest - S.FloorZ));
    }

    if (Action == EPSLDinoAction::Death && S.FirstDeathAt < 0.0)
    {
        S.FirstDeathAt = Since;
    }
    else if (S.FirstDeathAt >= 0.0 && Action != EPSLDinoAction::Death && !S.bLeftDeath)
    {
        S.bLeftDeath = true;
        S.LeftDeathTo = FString::Printf(TEXT("%s at %.3f s"), ActionName(Action), Since);
    }
    if (Since >= 0.2)
    {
        S.MinWeight = FMath::Min(S.MinWeight, Weight);
        S.MaxWeight = FMath::Max(S.MaxWeight, Weight);
    }
    if (Since >= 2.2 && !S.bHave22)
    {
        S.bHave22 = true;
        S.PelvisAbove22 = Pelvis - S.FloorZ;
    }
    if (Since >= 2.2)
    {
        S.MinHoldZ = FMath::Min(S.MinHoldZ, Pelvis);
        S.MaxHoldZ = FMath::Max(S.MaxHoldZ, Pelvis);
    }
    if (Since >= 3.0 && !S.bHave30)
    {
        S.bHave30 = true;
        S.Lowest30 = Lowest - S.FloorZ;
    }
    if (FApp::CanEverRender() && S.NextShot < static_cast<int32>(UE_ARRAY_COUNT(DeathShotTimes)) && Since >= DeathShotTimes[S.NextShot])
    {
        const FString Label = FString::Printf(TEXT("death_%d"), S.NextShot + 1);
        CapturePieViewport(Ctx, Label, ShotPath(Ctx, Label), true);
        T.AddInfo(FString::Printf(TEXT("%s captured at %.3f s after death"), *Label, Since));
        ++S.NextShot;
    }
    if (Since < 4.0)
    {
        return false;
    }

    const double HoldDrift = S.MaxHoldZ - S.MinHoldZ;
    T.AddInfo(FString::Printf(TEXT("{\"step\":\"death_result\",\"first_death_s\":%.3f,\"left_death\":\"%s\",\"min_weight\":%.4f,\"max_weight\":%.4f,\"stand_pelvis_above_floor\":%.1f,\"pelvis_above_floor_2_2s\":%.1f,\"hold_drift_2_2_to_4_0\":%.2f,\"lowest_bone_above_floor_3_0s\":%.1f,\"capsule_move\":%.2f,\"floor_z\":%.1f,\"ticks\":%d}"),
        S.FirstDeathAt, S.bLeftDeath ? *S.LeftDeathTo : TEXT("never"), S.MinWeight, S.MaxWeight, S.StandPelvisAbove, S.PelvisAbove22, HoldDrift, S.Lowest30,
        S.MaxCapsuleMove, S.FloorZ, S.Ticks));
    T.TestTrue(FString::Printf(TEXT("ActiveAction is Death within 0.2 s of the kill (%.3f s)"), S.FirstDeathAt), S.FirstDeathAt >= 0.0 && S.FirstDeathAt <= 0.2);
    T.TestFalse(FString::Printf(TEXT("Action stays Death until 4.0 s (left: %s)"), S.bLeftDeath ? *S.LeftDeathTo : TEXT("never")), S.bLeftDeath);
    T.TestTrue(FString::Printf(TEXT("ActionWeight is 1 +- 0.01 from 0.2 s on (min %.4f, max %.4f)"), S.MinWeight, S.MaxWeight),
        S.MinWeight >= 0.99 && S.MaxWeight <= 1.01);
    T.TestTrue(FString::Printf(TEXT("Pelvis above floor at 2.2 s (%.1f) <= 0.5 x standing (%.1f)"), S.PelvisAbove22, S.StandPelvisAbove),
        S.bHave22 && S.StandPelvisAbove > 0.0 && S.PelvisAbove22 <= 0.5 * S.StandPelvisAbove);
    T.TestTrue(FString::Printf(TEXT("Pelvis held: world Z changes <= 2 uu from 2.2 s to 4.0 s (%.2f)"), HoldDrift), HoldDrift <= 2.0);
    T.TestTrue(FString::Printf(TEXT("Lowest bone at 3.0 s within [floor-10, floor+40] (%.1f above floor)"), S.Lowest30),
        S.bHave30 && S.Lowest30 >= -10.0 && S.Lowest30 <= 40.0);
    T.TestTrue(FString::Printf(TEXT("Capsule moved <= 5 uu (%.2f)"), S.MaxCapsuleMove), S.MaxCapsuleMove <= 5.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLHeroDeathTest, "PSL.Dev.HeroTrex.Death", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLHeroDeathTest::RunTest(const FString& Parameters)
{
    if (!FPackageName::DoesPackageExist(DeathTestHeroMeshPackage))
    {
        AddWarning(TEXT("hero assets absent: death test skipped"));
        return true;
    }
    if (!SetIntCvar(TEXT("psl.Dev.HeroTrex"), 1))
    {
        AddWarning(TEXT("hero assets absent: death test skipped (psl.Dev.HeroTrex could not be set)"));
        SetIntCvar(TEXT("psl.Dev.HeroTrex"), 0);
        return true;
    }
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("HeroDeath"));
    if (!QueuePieStart(*this, Context, ArenaMapPath))
    {
        SetIntCvar(TEXT("psl.Dev.HeroTrex"), 0);
        return false;
    }
    const TSharedRef<FHeroDeathState> S = MakeShared<FHeroDeathState>();
    AddPieStep(Context, TEXT("SpawnDeathHero"), 10.0f, [S](FPSLPieCtx& Ctx)
    {
        if (Ctx.IsUsable() && !SpawnDeathHero(Ctx, *S))
        {
            Ctx.MarkInvalid(TEXT("Death hero setup failed"));
        }
        return true;
    });
    // Idle 0.5 s of world time so the pose is settled, then measure the standing reference.
    AddPieStep(Context, TEXT("DeathIdle"), 10.0f, [S](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !S->Hero.IsValid())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        S->IdleStart = S->IdleStart < 0.0 ? Now : S->IdleStart;
        if (Now - S->IdleStart < 0.5)
        {
            return false;
        }
        APSLCharacter* Hero = S->Hero.Get();
        S->StandPelvisAbove = PelvisZ(Hero->GetMesh()) - S->FloorZ;
        S->CapsuleStart = Hero->GetActorLocation();
        Ctx.Test->AddInfo(FString::Printf(TEXT("{\"step\":\"death_stand\",\"pelvis_above_floor\":%.1f,\"lowest_bone_above_floor\":%.1f,\"capsule\":[%.1f,%.1f,%.1f]}"),
            S->StandPelvisAbove, LowestBoneZ(Hero->GetMesh()) - S->FloorZ, S->CapsuleStart.X, S->CapsuleStart.Y, S->CapsuleStart.Z));
        return true;
    });
    AddPieStep(Context, TEXT("Kill"), 5.0f, [S](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !S->Hero.IsValid())
        {
            return true;
        }
        APSLCharacter* Hero = S->Hero.Get();
        // death_0 one tick before the kill, so its ReadPixels hitch lands in this tick's delta, not after the kill.
        if (FApp::CanEverRender() && !S->bShot0)
        {
            S->bShot0 = true;
            CapturePieViewport(Ctx, TEXT("death_0"), ShotPath(Ctx, TEXT("death_0")), true);
            Ctx.Test->AddInfo(TEXT("death_0 captured one tick before the kill"));
            return false;
        }
        const UPSLStatsComponent* Stats = Hero->GetStatsComponent();
        if (!Ctx.Test->TestNotNull(TEXT("Death hero stats component"), Stats))
        {
            Ctx.MarkInvalid(TEXT("Death hero has no stats component"));
            return true;
        }
        UGameplayStatics::ApplyDamage(Hero, Stats->GetMaxHealth() * 2.0f + 10.0f, nullptr, nullptr, UDamageType::StaticClass());
        Ctx.Test->TestTrue(TEXT("Death hero is dead after ApplyDamage"), Stats->IsDead());
        S->DeathWorldTime = Ctx.World->GetTimeSeconds();
        return true;
    });
    AddPieStep(Context, TEXT("DeathSequence"), 30.0f, [S](FPSLPieCtx& Ctx)
    {
        return !Ctx.IsUsable() || S->DeathWorldTime < 0.0 || TickDeath(Ctx, *S);
    });
    AddPieStep(Context, TEXT("Cleanup"), 2.0f, [S](FPSLPieCtx& Ctx)
    {
        SetIntCvar(TEXT("psl.Dev.HeroTrex"), 0);
        S->Shot->RestoreView(Ctx);
        if (S->Hero.IsValid())
        {
            S->Hero->Destroy();
        }
        return true;
    });
    QueuePieEnd(Context);
    return true;
}

} // namespace

#endif
