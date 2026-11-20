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

// One-shot full-body actions layered over locomotion. Death holds its last frame.
UENUM(BlueprintType)
enum class EPSLDinoAction : uint8
{
    None,
    Attack,
    Death
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

    static constexpr float ActionBlendIn = 0.1f;
    static constexpr float ActionBlendOut = 0.15f;

    // Action blend weight at TimeSinceStart: ramps in over ActionBlendIn; unless bHoldLast, ramps out so it
    // reaches 0 exactly at ClipLength. A held action (Death) stays at full weight.
    static float ComputeActionWeight(float TimeSinceStart, float ClipLength, bool bHoldLast);

    // Game thread: restarts the action clip. Missing clip -> warning, request ignored, locomotion unaffected.
    void PlayAction(EPSLDinoAction Action);

    EPSLDinoAction GetActiveAction() const { return ActiveAction; }
    float GetActionWeight() const { return ActionWeight; }

    // Read by the proxy in PreUpdate; id changes on every PlayAction.
    EPSLDinoAction GetRequestedAction() const { return RequestedAction; }
    uint32 GetActionRequestId() const { return ActionRequestId; }

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> IdleSequence;

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> WalkSequence;

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> RunSequence;

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> AttackSequence;

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> DeathSequence;

    // Mirrored from the anim proxy after each update (PostUpdate) so game code and tests can read it.
    UPROPERTY(Transient, BlueprintReadOnly, Category="Action")
    EPSLDinoAction ActiveAction;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Action")
    float ActionWeight;

    // FInterpTo speed for the ground-speed smoothing that feeds the blend.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion", meta=(ClampMin="0.0"))
    float SpeedInterpRate;

    // Attack clip playback rate, set by APSLCharacter from the species; the proxy reads it when an Attack starts.
    UPROPERTY(Transient)
    float AttackPlayRate = 1.0f;

    // Set by APSLCharacter from the species; 0 = Walk/Run always play at rate 1.
    UPROPERTY(Transient)
    float WalkAnimSpeed = 0.0f;

    UPROPERTY(Transient)
    float RunAnimSpeed = 0.0f;

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
    EPSLDinoAction RequestedAction;
    uint32 ActionRequestId;
};
