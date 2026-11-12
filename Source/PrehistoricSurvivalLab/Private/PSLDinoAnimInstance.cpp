#include "PSLDinoAnimInstance.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "GameFramework/Pawn.h"

FPSLLocomotionWeights UPSLDinoAnimInstance::ComputeLocomotionWeights(float GroundSpeed)
{
    const float Speed = FMath::Clamp(GroundSpeed, 0.0f, RunSpeed);
    FPSLLocomotionWeights Weights;
    if (Speed <= WalkSpeed)
    {
        Weights.Walk = Speed / WalkSpeed;
        Weights.Idle = 1.0f - Weights.Walk;
    }
    else
    {
        Weights.Run = (Speed - WalkSpeed) / (RunSpeed - WalkSpeed);
        Weights.Walk = 1.0f - Weights.Run;
    }
    return Weights;
}

namespace
{
struct FPSLDinoAnimProxy : public FAnimInstanceProxy
{
    FPSLDinoAnimProxy() = default;

    explicit FPSLDinoAnimProxy(UAnimInstance* InInstance)
        : FAnimInstanceProxy(InInstance)
    {
    }

    enum { NumNodes = 3 };

    FAnimNode_SequencePlayer_Standalone Nodes[NumNodes];
    float TargetSpeed = 0.0f;
    float SmoothedSpeed = 0.0f;
    float InterpRate = 8.0f;
    float Weights[NumNodes] = {1.0f, 0.0f, 0.0f};

    virtual void Initialize(UAnimInstance* InAnimInstance) override
    {
        FAnimInstanceProxy::Initialize(InAnimInstance);
        FAnimationInitializeContext Context(this);
        for (FAnimNode_SequencePlayer_Standalone& Node : Nodes)
        {
            Node.Initialize_AnyThread(Context);
        }
    }

    virtual void InitializeObjects(UAnimInstance* InAnimInstance) override
    {
        FAnimInstanceProxy::InitializeObjects(InAnimInstance);
        const UPSLDinoAnimInstance* Dino = CastChecked<UPSLDinoAnimInstance>(InAnimInstance);
        Nodes[0].SetSequence(Dino->IdleSequence);
        Nodes[1].SetSequence(Dino->WalkSequence);
        Nodes[2].SetSequence(Dino->RunSequence);
        InterpRate = Dino->SpeedInterpRate;
    }

    virtual void ClearObjects() override
    {
        FAnimInstanceProxy::ClearObjects();
        for (int32 Index = 0; Index < NumNodes; ++Index)
        {
            Nodes[Index].SetSequence(nullptr);
        }
    }

    virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
        const APawn* Pawn = InAnimInstance->TryGetPawnOwner();
        TargetSpeed = Pawn ? Pawn->GetVelocity().Size2D() : 0.0f;
    }

    virtual void Update(float DeltaSeconds) override
    {
        SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, TargetSpeed, DeltaSeconds, InterpRate);
        const FPSLLocomotionWeights W = UPSLDinoAnimInstance::ComputeLocomotionWeights(SmoothedSpeed);
        Weights[0] = W.Idle;
        Weights[1] = W.Walk;
        Weights[2] = W.Run;
    }

    virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override
    {
        for (int32 Index = 0; Index < NumNodes; ++Index)
        {
            if (Weights[Index] > ZERO_ANIMWEIGHT_THRESH)
            {
                Nodes[Index].Update_AnyThread(InContext.FractionalWeight(Weights[Index]));
            }
        }
    }

    virtual bool Evaluate(FPoseContext& Output) override
    {
        float Accumulated = 0.0f;
        for (int32 Index = 0; Index < NumNodes; ++Index)
        {
            const float Weight = Weights[Index];
            if (Weight <= ZERO_ANIMWEIGHT_THRESH)
            {
                continue;
            }

            if (Accumulated == 0.0f)
            {
                Nodes[Index].Evaluate_AnyThread(Output);
            }
            else
            {
                FPoseContext Next(Output);
                Nodes[Index].Evaluate_AnyThread(Next);
                FAnimationPoseData OutData(Output);
                const FAnimationPoseData NextData(Next);
                FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutData, NextData, Accumulated / (Accumulated + Weight));
            }
            Accumulated += Weight;
        }

        if (Accumulated == 0.0f)
        {
            Output.ResetToRefPose();
        }
        return true;
    }
};
}

UPSLDinoAnimInstance::UPSLDinoAnimInstance()
{
    SpeedInterpRate = 8.0f;
    RootMotionMode = ERootMotionMode::NoRootMotionExtraction;
}

FAnimInstanceProxy* UPSLDinoAnimInstance::CreateAnimInstanceProxy()
{
    return new FPSLDinoAnimProxy(this);
}
