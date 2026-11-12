#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "PSLDinoAnimInstance.generated.h"

class UAnimSequence;

struct FPSLLocomotionWeights
{
    float Idle = 0.0f;
    float Walk = 0.0f;
    float Run = 0.0f;
};

/**
 * Code-only Idle/Walk/Run locomotion: three sequence players blended by smoothed ground speed.
 * No Animation Blueprint asset and no root motion; the capsule drives movement.
 */
UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLDinoAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    UPSLDinoAnimInstance();

    // 0 -> Idle, WalkSpeed -> Walk, RunSpeed -> Run; linear in between, clamped, always sums to 1.
    static FPSLLocomotionWeights ComputeLocomotionWeights(float GroundSpeed);

    static constexpr float WalkSpeed = 300.0f;
    static constexpr float RunSpeed = 600.0f;

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> IdleSequence;

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> WalkSequence;

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> RunSequence;

    // FInterpTo speed for the ground-speed smoothing that feeds the blend.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion", meta=(ClampMin="0.0"))
    float SpeedInterpRate;

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};
