#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PSLCharacter.h"
#include "PSLDinoAnimInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLocomotionWeightsTest, "PSL.Anim.LocomotionWeights", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLLocomotionWeightsTest::RunTest(const FString& Parameters)
{
    constexpr float Tol = KINDA_SMALL_NUMBER;
    struct FCase { const TCHAR* Name; float Speed; float Idle; float Walk; float Run; };
    const FCase Cases[] = {
        {TEXT("0 -> Idle"), 0.0f, 1.0f, 0.0f, 0.0f},
        {TEXT("300 -> Walk"), 300.0f, 0.0f, 1.0f, 0.0f},
        {TEXT("600 -> Run"), 600.0f, 0.0f, 0.0f, 1.0f},
        {TEXT("450 -> Walk/Run 50:50"), 450.0f, 0.0f, 0.5f, 0.5f},
        {TEXT("150 -> Idle/Walk 50:50"), 150.0f, 0.5f, 0.5f, 0.0f},
        {TEXT("below 0 clamps to Idle"), -100.0f, 1.0f, 0.0f, 0.0f},
        {TEXT("above 600 clamps to Run"), 900.0f, 0.0f, 0.0f, 1.0f},
    };

    for (const FCase& Case : Cases)
    {
        const FPSLLocomotionWeights W = UPSLDinoAnimInstance::ComputeLocomotionWeights(Case.Speed);
        TestEqual(FString::Printf(TEXT("%s: Idle"), Case.Name), W.Idle, Case.Idle, Tol);
        TestEqual(FString::Printf(TEXT("%s: Walk"), Case.Name), W.Walk, Case.Walk, Tol);
        TestEqual(FString::Printf(TEXT("%s: Run"), Case.Name), W.Run, Case.Run, Tol);
    }

    for (float Speed = -50.0f; Speed <= 700.0f; Speed += 12.5f)
    {
        const FPSLLocomotionWeights W = UPSLDinoAnimInstance::ComputeLocomotionWeights(Speed);
        TestEqual(FString::Printf(TEXT("Weights sum to 1 at speed %.1f"), Speed), W.Idle + W.Walk + W.Run, 1.0f, Tol);
        TestTrue(TEXT("Weights are non-negative"), W.Idle >= 0.0f && W.Walk >= 0.0f && W.Run >= 0.0f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLDinoDefaultsTest, "PSL.Character.DinoDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLDinoDefaultsTest::RunTest(const FString& Parameters)
{
    const APSLCharacter* Character = GetDefault<APSLCharacter>();
    const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
    const USkeletalMeshComponent* Mesh = Character->GetMesh();

    TestEqual(TEXT("Biped capsule radius"), Capsule->GetUnscaledCapsuleRadius(), APSLCharacter::DinoCapsuleRadius);
    TestEqual(TEXT("Biped capsule half height"), Capsule->GetUnscaledCapsuleHalfHeight(), APSLCharacter::DinoCapsuleHalfHeight);
    TestEqual(TEXT("Mesh drops to capsule bottom"), static_cast<double>(Mesh->GetRelativeLocation().Z), static_cast<double>(-APSLCharacter::DinoCapsuleHalfHeight));
    TestEqual(TEXT("Mesh yaw"), static_cast<double>(Mesh->GetRelativeRotation().Yaw), static_cast<double>(APSLCharacter::DinoMeshYaw));

    TestEqual(TEXT("Mesh path"), Character->DinoMesh.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Characters/Dino/SK_Trex.SK_Trex")));
    TestEqual(TEXT("Anim class path"), Character->DinoAnimClass.ToSoftObjectPath().ToString(), FString(TEXT("/Script/PrehistoricSurvivalLab.PSLDinoAnimInstance")));
    TestFalse(TEXT("Idle path set"), Character->DinoIdleAnim.IsNull());
    TestFalse(TEXT("Walk path set"), Character->DinoWalkAnim.IsNull());
    TestFalse(TEXT("Run path set"), Character->DinoRunAnim.IsNull());

    const UPSLDinoAnimInstance* Anim = GetDefault<UPSLDinoAnimInstance>();
    TestEqual(TEXT("No root motion extraction"), static_cast<int32>(Anim->RootMotionMode), static_cast<int32>(ERootMotionMode::NoRootMotionExtraction));

    // Existing movement defaults stay intact.
    TestEqual(TEXT("Walk speed"), Character->WalkSpeed, 300.0f);
    TestEqual(TEXT("Sprint speed"), Character->SprintSpeed, 600.0f);
    return true;
}

#endif
