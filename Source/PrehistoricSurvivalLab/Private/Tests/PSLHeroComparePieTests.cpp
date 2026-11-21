#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "PSLPieTestSupport.h"
#include "PSLDinoSpecies.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"

// PSL.Dev.HeroTrex.Compare: controlled old-vs-new stills in L_TestArena (cube floor, one sun, one sky light, no foliage).
// Same stage, same camera transform and the same fixed manual exposure for the hero (player, cvar 1) and the old
// SK_Trex (spawned, controller-less, cvar 0). Under -nullrhi the staging and asserts run; captures are skipped.
namespace
{
using namespace PSLPie;

// Unity builds merge anonymous namespaces: names here must not collide with the other PSLHero*PieTests.cpp files.
constexpr const TCHAR* CompareHeroMeshPackage = TEXT("/Game/Dev/ArtDir/Hero/SK_HeroTrex");
constexpr const TCHAR* CompareHeroMeshPath = TEXT("/Game/Dev/ArtDir/Hero/SK_HeroTrex.SK_HeroTrex");
constexpr const TCHAR* CompareOldMeshPath = TEXT("/Game/Characters/Dino/SK_Trex.SK_Trex");
// Manual exposure, physical-camera exposure off (EV100 0), plus this bias; identical for every Compare shot.
// ponytail: one constant for the L_TestArena sun; recalibrate here if the arena lighting changes.
constexpr float CompareExposureBias = -1.0f;
// Face close-up camera offset from the head point (subject faces +X): front, side, up.
const FVector CompareFaceOffset(220.0, 160.0, 20.0);

struct FCompareState
{
    TSharedRef<FShotState> Shot = MakeShared<FShotState>(); // Shot->Other is the old T-Rex; RestoreView destroys it and the camera
    FVector Stage = FVector::ZeroVector; // capsule centre of a subject standing on the stage (both species: capsule HH 120)
    FVector Park = FVector::ZeroVector;  // capsule centre of the hidden subject, away from the stage
    double BodyLen = 0.0;                // max of both subjects' mesh bounds length
    bool bExposureLogged = false;
    int32 IdleFrames = 0;
    TArray<TWeakObjectPtr<APSLCharacter>> ShotSubjects; // visible subjects of the shot being staged
    TArray<TWeakObjectPtr<APawn>> HiddenPawns; // other pawns (prey/predator) hidden for the whole test, shown again in Cleanup
};

APSLCharacter* CompareOld(const FCompareState& S)
{
    return S.Shot->Other.Get();
}

// Static floor below (X, Y) (WorldStatic only: pawn capsules are ignored); true when it is within 300 uu of RefZ.
bool CompareFloorAt(UWorld* World, double X, double Y, double RefZ, double& OutZ)
{
    FHitResult Hit;
    if (World->LineTraceSingleByObjectType(Hit, FVector(X, Y, RefZ + 3000.0), FVector(X, Y, RefZ - 3000.0),
            FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(TEXT("PSLHeroCompareFloor"), false))
        && FMath::Abs(Hit.ImpactPoint.Z - RefZ) <= 300.0)
    {
        OutZ = Hit.ImpactPoint.Z;
        return true;
    }
    return false;
}

double CompareBodyLength(const USkeletalMeshComponent* Mesh)
{
    return 2.0 * FMath::Max(Mesh->Bounds.BoxExtent.X, Mesh->Bounds.BoxExtent.Y);
}

// DEF-head (hero), else the first bone whose name contains "head" (old rig), else the front-top of the bounds.
FVector CompareHeadPoint(const USkeletalMeshComponent* Mesh, FString& OutSource)
{
    const FName DefHead(TEXT("DEF-head"));
    if (Mesh->GetBoneIndex(DefHead) != INDEX_NONE)
    {
        OutSource = DefHead.ToString();
        return Mesh->GetBoneLocation(DefHead, EBoneSpaces::WorldSpace);
    }
    for (int32 Bone = 0; Bone < Mesh->GetNumBones(); ++Bone)
    {
        const FName Name = Mesh->GetBoneName(Bone);
        if (Name.ToString().Contains(TEXT("head")))
        {
            OutSource = Name.ToString();
            return Mesh->GetBoneLocation(Name, EBoneSpaces::WorldSpace);
        }
    }
    OutSource = TEXT("bounds front-top");
    const FBoxSphereBounds& B = Mesh->Bounds;
    return B.Origin + FVector(B.BoxExtent.X, 0.0, B.BoxExtent.Z * 0.5);
}

EPSLDinoAction CompareAction(const APSLCharacter* C)
{
    const UPSLDinoAnimInstance* Anim = Cast<UPSLDinoAnimInstance>(C->GetMesh()->GetAnimInstance());
    return Anim ? Anim->GetActiveAction() : EPSLDinoAction::Death; // no anim instance never counts as idle
}

// UseCamera plus the fixed manual exposure on the camera's own post-process settings (blend weight 1).
void CompareUseCamera(FPSLPieCtx& Ctx, FCompareState& S, const FVector& Spot, const FVector& LookAt)
{
    UseCamera(Ctx, *S.Shot, Spot, LookAt);
    UCameraComponent* Cam = S.Shot->Cam->GetCameraComponent();
    FPostProcessSettings& PP = Cam->PostProcessSettings;
    PP.bOverride_AutoExposureMethod = true;
    PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    PP.AutoExposureApplyPhysicalCameraExposure = false;
    PP.bOverride_AutoExposureBias = true;
    PP.AutoExposureBias = CompareExposureBias;
    Cam->PostProcessBlendWeight = 1.0f;
    if (!S.bExposureLogged)
    {
        S.bExposureLogged = true;
        Ctx.Test->AddInfo(FString::Printf(TEXT("{\"step\":\"cmp_exposure\",\"method\":\"AEM_Manual\",\"physical_camera\":false,\"bias\":%.2f,\"fov\":%.1f}"),
            CompareExposureBias, Cam->FieldOfView));
    }
}

// Puts a subject's capsule centre at Location facing +X and shows or hides it. The old T-Rex has no controller, so its
// CharacterMovement does not simulate (no gravity); its collision follows visibility. The player keeps its collision.
void ComparePlace(FPSLPieCtx& Ctx, APSLCharacter* C, const FVector& Location, bool bVisible)
{
    if (C == Ctx.Character.Get())
    {
        TeleportTo(Ctx, Location);
        Ctx.PC->SetControlRotation(FRotator::ZeroRotator);
    }
    else
    {
        C->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
        C->SetActorEnableCollision(bVisible);
    }
    C->SetActorRotation(FRotator::ZeroRotator);
    C->SetActorHiddenInGame(!bVisible);
}

// Camera-to-subject foliage/ISM occluders must be zero for every visible subject (always asserted, never conditional).
void CompareAssertClear(FPSLPieCtx& Ctx, const FCompareState& S, const FString& Label, const FVector& Cam)
{
    for (const TWeakObjectPtr<APSLCharacter>& C : S.ShotSubjects)
    {
        if (C.IsValid())
        {
            const int32 Count = OccluderCount(Ctx.World, Cam, C->GetMesh()->Bounds.Origin, Ctx.Character.Get(), CompareOld(S));
            Ctx.Test->TestEqual(FString::Printf(TEXT("%s: ISM occluders camera-to-%s"), *Label, C.Get() == Ctx.Character.Get() ? TEXT("hero") : TEXT("old")), Count, 0);
        }
    }
}

// Screen-space check of the shot subjects (needs a renderer and a view already updated to the shot camera).
void CompareAssertProjected(FPSLPieCtx& Ctx, const FCompareState& S, const FString& Label)
{
    int32 W = 0, H = 0;
    Ctx.PC->GetViewportSize(W, H);
    for (const TWeakObjectPtr<APSLCharacter>& C : S.ShotSubjects)
    {
        if (!Ctx.Test->TestTrue(FString::Printf(TEXT("%s: subject still valid"), *Label), C.IsValid()))
        {
            continue;
        }
        const FBoxSphereBounds& B = C->GetMesh()->Bounds;
        FVector2D Centre, Top, Bottom;
        const bool bOk = Ctx.PC->ProjectWorldLocationToScreen(B.Origin, Centre)
            && Ctx.PC->ProjectWorldLocationToScreen(B.Origin + FVector(0.0, 0.0, B.BoxExtent.Z), Top)
            && Ctx.PC->ProjectWorldLocationToScreen(B.Origin - FVector(0.0, 0.0, B.BoxExtent.Z), Bottom);
        const TCHAR* Who = C.Get() == Ctx.Character.Get() ? TEXT("hero") : TEXT("old");
        Ctx.Test->AddInfo(FString::Printf(TEXT("{\"step\":\"cmp_project\",\"shot\":\"%s\",\"subject\":\"%s\",\"centre\":[%.0f,%.0f],\"bounds_px_height\":%.0f,\"viewport\":[%d,%d]}"),
            *Label, Who, Centre.X, Centre.Y, FMath::Abs(Bottom.Y - Top.Y), W, H));
        Ctx.Test->TestTrue(FString::Printf(TEXT("%s: %s bounds centre projects inside the viewport"), *Label, Who),
            bOk && Centre.X >= 0.0 && Centre.X <= W && Centre.Y >= 0.0 && Centre.Y <= H);
    }
}

// Setup fills S.ShotSubjects, places/hides the subjects and returns the camera spot and look-at point.
using FCompareStage = TFunction<bool(FPSLPieCtx&, FCompareState&, FVector& OutCam, FVector& OutLookAt)>;

// Two steps per shot: stage (place, camera, occluder asserts), then after 4 PIE ticks the projection asserts and the
// capture (both only with a renderer).
void QueueCompareShot(const TSharedRef<FPSLPieCtx>& Context, const TSharedRef<FCompareState>& S, const FString& Label, FCompareStage Stage)
{
    AddPieStep(Context, TEXT("CompareStage"), 5.0f, [S, Label, Stage = MoveTemp(Stage)](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !CompareOld(*S))
        {
            return true;
        }
        S->ShotSubjects.Reset();
        FVector Cam, LookAt;
        if (!Stage(Ctx, *S, Cam, LookAt))
        {
            Ctx.MarkInvalid(FString::Printf(TEXT("%s: staging failed"), *Label));
            return true;
        }
        CompareUseCamera(Ctx, *S, Cam, LookAt);
        Ctx.Test->AddInfo(FString::Printf(TEXT("{\"step\":\"cmp_camera\",\"shot\":\"%s\",\"camera\":[%.1f,%.1f,%.1f],\"look_at\":[%.1f,%.1f,%.1f]}"),
            *Label, Cam.X, Cam.Y, Cam.Z, LookAt.X, LookAt.Y, LookAt.Z));
        CompareAssertClear(Ctx, *S, Label, Cam);
        return true;
    });
    AddPieStep(Context, TEXT("CompareShot"), 10.0f, [S, Label, Frames = 0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable() || !FApp::CanEverRender())
        {
            return true;
        }
        if (++Frames < 4)
        {
            return false;
        }
        CompareAssertProjected(Ctx, *S, Label);
        CapturePieViewport(Ctx, Label, ShotPath(Ctx, Label), true);
        Record(Ctx, *Label);
        return true;
    });
}

// Spawns the old T-Rex (cvar 0 -> SK_Trex, cvar back to 1), hides other pawns, measures the stage and logs both subjects.
bool CompareSetup(FPSLPieCtx& Ctx, FCompareState& S)
{
    FAutomationTestBase& T = *Ctx.Test;
    APSLCharacter* Hero = Ctx.Character.Get();
    const double HeroHalf = Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Start = Hero->GetActorLocation(); // the PIE pawn spawned at the PlayerStart
    double FloorZ = 0.0;
    if (!CompareFloorAt(Ctx.World, Start.X, Start.Y, Start.Z - HeroHalf, FloorZ))
    {
        Ctx.MarkInvalid(TEXT("No arena floor below the PlayerStart"));
        return false;
    }
    for (TActorIterator<APawn> It(Ctx.World); It; ++It)
    {
        if (*It != Hero)
        {
            // Frozen in place (no actor/movement tick) so no prey wanders into a shot and no AI reacts to the subjects.
            It->SetActorHiddenInGame(true);
            It->SetActorTickEnabled(false);
            if (UPawnMovementComponent* Move = It->GetMovementComponent())
            {
                Move->SetComponentTickEnabled(false);
            }
            S.HiddenPawns.Add(*It);
        }
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSLCharacter* Old = Ctx.World->SpawnActor<APSLCharacter>(APSLCharacter::StaticClass(), Start + FVector(0.0, 0.0, 3000.0), FRotator::ZeroRotator, Params);
    if (!T.TestNotNull(TEXT("Spawned old T-Rex APSLCharacter"), Old))
    {
        return false;
    }
    S.Shot->Other = Old;
    SetIntCvar(TEXT("psl.Dev.HeroTrex"), 0);
    Old->ApplySpecies(UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::TRexId, Old));
    SetIntCvar(TEXT("psl.Dev.HeroTrex"), 1);
    T.TestNull(TEXT("Old T-Rex has no controller"), Old->GetController());

    const USkeletalMesh* HeroAsset = Hero->GetMesh()->GetSkeletalMeshAsset();
    const USkeletalMesh* OldAsset = Old->GetMesh()->GetSkeletalMeshAsset();
    const bool bHero = T.TestTrue(TEXT("Subject A (player) mesh is SK_HeroTrex"), HeroAsset && HeroAsset->GetPathName() == CompareHeroMeshPath);
    const bool bOld = T.TestTrue(TEXT("Subject B (spawned) mesh is SK_Trex"), OldAsset && OldAsset->GetPathName() == CompareOldMeshPath);
    if (!bHero || !bOld)
    {
        return false;
    }
    const double OldHalf = Old->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    T.TestEqual(TEXT("Hero and old capsule half-heights are equal (shared stage height)"), HeroHalf, OldHalf);
    S.Stage = FVector(Start.X, Start.Y, FloorZ + HeroHalf);
    S.Park = S.Stage + FVector(0.0, 1800.0, 0.0);
    double ParkFloor = 0.0;
    if (!CompareFloorAt(Ctx.World, S.Park.X, S.Park.Y, FloorZ, ParkFloor))
    {
        Ctx.MarkInvalid(TEXT("No arena floor at the park spot (PlayerStart + Y 1800)"));
        return false;
    }
    for (APSLCharacter* C : {Hero, Old})
    {
        C->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    }
    ComparePlace(Ctx, Hero, S.Stage, true);
    ComparePlace(Ctx, Old, S.Park, true);
    T.AddInfo(FString::Printf(TEXT("{\"step\":\"cmp_setup\",\"floor_z\":%.1f,\"stage\":[%.1f,%.1f,%.1f],\"hero_capsule_hh\":%.1f,\"old_capsule_hh\":%.1f,\"hero_mesh_scale\":%.4f,\"old_mesh_scale\":%.4f}"),
        FloorZ, S.Stage.X, S.Stage.Y, S.Stage.Z, HeroHalf, OldHalf, Hero->GetMesh()->GetRelativeScale3D().X, Old->GetMesh()->GetRelativeScale3D().X));
    return true;
}

// Both idle: speed < 1 uu/s and ActiveAction None for 30 consecutive frames. Then logs the rendered (bounds) heights.
bool CompareIdle(FPSLPieCtx& Ctx, FCompareState& S)
{
    APSLCharacter* Hero = Ctx.Character.Get();
    APSLCharacter* Old = CompareOld(S);
    if (!Old)
    {
        Ctx.MarkInvalid(TEXT("Old T-Rex destroyed before the idle check"));
        return true;
    }
    bool bIdle = true;
    for (const APSLCharacter* C : std::initializer_list<const APSLCharacter*>{Hero, Old})
    {
        bIdle &= C->GetVelocity().Size() < 1.0 && CompareAction(C) == EPSLDinoAction::None;
    }
    S.IdleFrames = bIdle ? S.IdleFrames + 1 : 0;
    if (S.IdleFrames < 30)
    {
        return false;
    }
    const USkeletalMeshComponent* HM = Hero->GetMesh();
    const USkeletalMeshComponent* OM = Old->GetMesh();
    S.BodyLen = FMath::Max(CompareBodyLength(HM), CompareBodyLength(OM));
    const double HeroH = 2.0 * HM->Bounds.BoxExtent.Z;
    const double OldH = 2.0 * OM->Bounds.BoxExtent.Z;
    Ctx.Test->AddInfo(FString::Printf(TEXT("{\"step\":\"cmp_idle\",\"hero_bounds_height\":%.1f,\"old_bounds_height\":%.1f,\"hero_old_height_ratio\":%.3f,\"hero_length\":%.1f,\"old_length\":%.1f}"),
        HeroH, OldH, OldH > 0.0 ? HeroH / OldH : 0.0, CompareBodyLength(HM), CompareBodyLength(OM)));
    Ctx.Test->TestTrue(TEXT("Both subjects have non-empty mesh bounds"), HeroH > 0.0 && OldH > 0.0 && S.BodyLen > 0.0);
    return true;
}

// The single-subject camera: perpendicular to the +X facing, FOV 60, body length ~60% of the frame, at capsule-centre height.
void CompareSingleCamera(const FCompareState& S, FVector& OutCam, FVector& OutLookAt)
{
    OutLookAt = S.Stage;
    OutCam = S.Stage - FVector(0.0, FrameDistance(S.BodyLen, 0.6), 0.0);
}

// Head close-up: same offset from each subject's head point.
bool CompareFaceCamera(FPSLPieCtx& Ctx, const APSLCharacter* C, FVector& OutCam, FVector& OutLookAt)
{
    FString Source;
    OutLookAt = CompareHeadPoint(C->GetMesh(), Source);
    OutCam = OutLookAt + FVector(CompareFaceOffset.X, -CompareFaceOffset.Y, CompareFaceOffset.Z);
    Ctx.Test->AddInfo(FString::Printf(TEXT("Face target for %s: %s"), C == Ctx.Character.Get() ? TEXT("hero") : TEXT("old"), *Source));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLHeroCompareTest, "PSL.Dev.HeroTrex.Compare", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLHeroCompareTest::RunTest(const FString& Parameters)
{
    if (!FPackageName::DoesPackageExist(CompareHeroMeshPackage))
    {
        AddWarning(TEXT("hero assets absent: compare skipped"));
        return true;
    }
    const int32 PrevHero = GetIntCvar(TEXT("psl.Dev.HeroTrex"));
    if (!SetIntCvar(TEXT("psl.Dev.HeroTrex"), 1))
    {
        AddWarning(TEXT("hero assets absent: compare skipped (psl.Dev.HeroTrex could not be set)"));
        SetIntCvar(TEXT("psl.Dev.HeroTrex"), PrevHero);
        return true;
    }
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("HeroCompare"));
    if (!SetPredatorSpawnCvar(1) || !QueuePieStart(*this, Context, ArenaMapPath))
    {
        SetIntCvar(TEXT("psl.Dev.HeroTrex"), PrevHero);
        return false;
    }
    const TSharedRef<FCompareState> S = MakeShared<FCompareState>();
    AddPieStep(Context, TEXT("CompareSetup"), 10.0f, [S](FPSLPieCtx& Ctx)
    {
        if (Ctx.IsUsable())
        {
            Ctx.PC->SetIgnoreLookInput(true);
            if (!CompareSetup(Ctx, *S))
            {
                Ctx.MarkInvalid(TEXT("Compare setup failed"));
            }
        }
        return true;
    });
    AddPieStep(Context, TEXT("CompareIdle"), 10.0f, [S](FPSLPieCtx& Ctx)
    {
        return !Ctx.IsUsable() || CompareIdle(Ctx, *S);
    });

    // A: hero alone on the stage; B parked out of frame, hidden, no collision.
    QueueCompareShot(Context, S, TEXT("cmp_hero"), [](FPSLPieCtx& Ctx, FCompareState& St, FVector& Cam, FVector& LookAt)
    {
        ComparePlace(Ctx, CompareOld(St), St.Park, false);
        ComparePlace(Ctx, Ctx.Character.Get(), St.Stage, true);
        St.ShotSubjects.Add(Ctx.Character.Get());
        CompareSingleCamera(St, Cam, LookAt);
        return true;
    });
    // B: old T-Rex at the identical stage and camera; the hero parked and hidden.
    QueueCompareShot(Context, S, TEXT("cmp_old"), [](FPSLPieCtx& Ctx, FCompareState& St, FVector& Cam, FVector& LookAt)
    {
        ComparePlace(Ctx, Ctx.Character.Get(), St.Park, false);
        ComparePlace(Ctx, CompareOld(St), St.Stage, true);
        St.ShotSubjects.Add(CompareOld(St));
        CompareSingleCamera(St, Cam, LookAt);
        return true;
    });
    // Side by side: old at -X, hero at +X, camera at the midpoint.
    QueueCompareShot(Context, S, TEXT("cmp_side_by_side"), [](FPSLPieCtx& Ctx, FCompareState& St, FVector& Cam, FVector& LookAt)
    {
        const double Offset = 0.6 * St.BodyLen;
        ComparePlace(Ctx, CompareOld(St), St.Stage - FVector(Offset, 0.0, 0.0), true);
        ComparePlace(Ctx, Ctx.Character.Get(), St.Stage + FVector(Offset, 0.0, 0.0), true);
        St.ShotSubjects.Add(Ctx.Character.Get());
        St.ShotSubjects.Add(CompareOld(St));
        LookAt = St.Stage;
        Cam = St.Stage - FVector(0.0, FrameDistance(2.0 * Offset + St.BodyLen, 0.8), 0.0);
        return true;
    });
    // Face close-ups, same head-relative framing for both.
    QueueCompareShot(Context, S, TEXT("cmp_hero_face"), [](FPSLPieCtx& Ctx, FCompareState& St, FVector& Cam, FVector& LookAt)
    {
        ComparePlace(Ctx, CompareOld(St), St.Park, false);
        ComparePlace(Ctx, Ctx.Character.Get(), St.Stage, true);
        St.ShotSubjects.Add(Ctx.Character.Get());
        return CompareFaceCamera(Ctx, Ctx.Character.Get(), Cam, LookAt);
    });
    QueueCompareShot(Context, S, TEXT("cmp_old_face"), [](FPSLPieCtx& Ctx, FCompareState& St, FVector& Cam, FVector& LookAt)
    {
        ComparePlace(Ctx, Ctx.Character.Get(), St.Park, false);
        ComparePlace(Ctx, CompareOld(St), St.Stage, true);
        St.ShotSubjects.Add(CompareOld(St));
        return CompareFaceCamera(Ctx, CompareOld(St), Cam, LookAt);
    });

    // HasObjects, not IsUsable: also restore after a failed or timed-out step.
    AddPieStep(Context, TEXT("Cleanup"), 2.0f, [S, PrevHero](FPSLPieCtx& Ctx)
    {
        SetIntCvar(TEXT("psl.Dev.HeroTrex"), PrevHero);
        SetPredatorSpawnCvar(1);
        for (const TWeakObjectPtr<APawn>& P : S->HiddenPawns)
        {
            if (P.IsValid())
            {
                P->SetActorHiddenInGame(false);
                P->SetActorTickEnabled(true);
                if (UPawnMovementComponent* Move = P->GetMovementComponent())
                {
                    Move->SetComponentTickEnabled(true);
                }
            }
        }
        if (Ctx.HasObjects())
        {
            Ctx.Character->SetActorHiddenInGame(false);
            Ctx.PC->ResetIgnoreLookInput();
        }
        S->Shot->RestoreView(Ctx);
        return true;
    });
    QueuePieEnd(Context);
    return true;
}

} // namespace

#endif
