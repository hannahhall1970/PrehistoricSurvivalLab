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

float UPSLDinoAnimInstance::ComputeActionWeight(float TimeSinceStart, float ClipLength, bool bHoldLast)
{
    if (ClipLength <= 0.0f || TimeSinceStart < 0.0f)
    {
        return 0.0f;
    }
    const float In = FMath::Clamp(TimeSinceStart / ActionBlendIn, 0.0f, 1.0f);
    const float Out = bHoldLast ? 1.0f : FMath::Clamp((ClipLength - TimeSinceStart) / ActionBlendOut, 0.0f, 1.0f);
    return In * Out;
}

void UPSLDinoAnimInstance::PlayAction(EPSLDinoAction Action)
{
    if (Action == EPSLDinoAction::None || RequestedAction == EPSLDinoAction::Death)
    {
        return;
    }

    const UAnimSequence* Clip = Action == EPSLDinoAction::Attack ? AttackSequence.Get() : DeathSequence.Get();
    if (!Clip)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s: no clip for dino action %d; locomotion continues."), *GetName(), static_cast<int32>(Action));
        return;
    }

    RequestedAction = Action;
    ++ActionRequestId;
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

    // Action layer: index 0 = Attack, 1 = Death (EPSLDinoAction minus one).
    FAnimNode_SequencePlayer_Standalone ActionNodes[2];
    uint32 SeenRequestId = 0;
    EPSLDinoAction ActionType = EPSLDinoAction::None;
    float ActionTime = 0.0f;
    float ActionClipLength = 0.0f;
    float ActionWeight = 0.0f;

    FAnimNode_SequencePlayer_Standalone Nodes[NumNodes];
    float TargetSpeed = 0.0f;
    float WalkAnimSpeed = 0.0f;
    float RunAnimSpeed = 0.0f;
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
        for (FAnimNode_SequencePlayer_Standalone& Node : ActionNodes)
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
        ActionNodes[0].SetSequence(Dino->AttackSequence);
        ActionNodes[1].SetSequence(Dino->DeathSequence);
        InterpRate = Dino->SpeedInterpRate;
    }

    virtual void ClearObjects() override
    {
        FAnimInstanceProxy::ClearObjects();
        for (int32 Index = 0; Index < NumNodes; ++Index)
        {
            Nodes[Index].SetSequence(nullptr);
        }
        for (FAnimNode_SequencePlayer_Standalone& Node : ActionNodes)
        {
            Node.SetSequence(nullptr);
        }
    }

    virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
        const APawn* Pawn = InAnimInstance->TryGetPawnOwner();
        TargetSpeed = Pawn ? Pawn->GetVelocity().Size2D() : 0.0f;
        const UPSLDinoAnimInstance* DinoCfg = CastChecked<UPSLDinoAnimInstance>(InAnimInstance);
        WalkAnimSpeed = DinoCfg->WalkAnimSpeed;
        RunAnimSpeed = DinoCfg->RunAnimSpeed;

        // A new request id restarts the matching one-shot node from the first frame.
        const UPSLDinoAnimInstance* Dino = CastChecked<UPSLDinoAnimInstance>(InAnimInstance);
        if (Dino->GetActionRequestId() != SeenRequestId)
        {
            SeenRequestId = Dino->GetActionRequestId();
            const EPSLDinoAction Requested = Dino->GetRequestedAction();
            UAnimSequence* Clip = Requested == EPSLDinoAction::Attack ? Dino->AttackSequence.Get() : Dino->DeathSequence.Get();
            if (Requested != EPSLDinoAction::None && Clip)
            {
                FAnimNode_SequencePlayer_Standalone& Node = ActionNodes[static_cast<int32>(Requested) - 1];
                const float Rate = Requested == EPSLDinoAction::Attack ? FMath::Max(Dino->AttackPlayRate, 0.01f) : 1.0f;
                const float ClipLength = Clip->GetPlayLength();
                float Start = 0.0f;
                float End = ClipLength;
                if (Requested == EPSLDinoAction::Attack)
                {
                    const bool bBadWindow = Dino->AttackStartTime >= ClipLength || (Dino->AttackEndTime > 0.0f && Dino->AttackEndTime <= Dino->AttackStartTime);
                    if (bBadWindow)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Dino attack window [%.3f, %.3f] invalid for clip length %.3f; playing the full clip."),
                            Dino->AttackStartTime, Dino->AttackEndTime, ClipLength);
                    }
                    else
                    {
                        Start = Dino->AttackStartTime;
                        End = Dino->AttackEndTime > 0.0f ? FMath::Min(Dino->AttackEndTime, ClipLength) : ClipLength;
                    }
                }
                Node.SetSequence(Clip);
                Node.SetLoopAnimation(false);
                Node.SetStartPosition(Start);
                Node.SetPlayRate(Rate);
                Node.Initialize_AnyThread(FAnimationInitializeContext(this));
                ActionType = Requested;
                ActionTime = 0.0f;
                ActionClipLength = (End - Start) / Rate;
                ActionWeight = 0.0f;
            }
        }
    }

    virtual void Update(float DeltaSeconds) override
    {
        SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, TargetSpeed, DeltaSeconds, InterpRate);
        if (WalkAnimSpeed > 0.0f)
        {
            Nodes[1].SetPlayRate(FMath::Clamp(SmoothedSpeed / WalkAnimSpeed, 0.5f, 6.0f));
        }
        if (RunAnimSpeed > 0.0f)
        {
            Nodes[2].SetPlayRate(FMath::Clamp(SmoothedSpeed / RunAnimSpeed, 0.5f, 3.0f));
        }
        const FPSLLocomotionWeights W = UPSLDinoAnimInstance::ComputeLocomotionWeights(SmoothedSpeed);
        Weights[0] = W.Idle;
        Weights[1] = W.Walk;
        Weights[2] = W.Run;

        if (ActionType != EPSLDinoAction::None)
        {
            ActionTime += DeltaSeconds;
            const bool bHold = ActionType == EPSLDinoAction::Death;
            ActionWeight = UPSLDinoAnimInstance::ComputeActionWeight(ActionTime, ActionClipLength, bHold);
            if (!bHold && ActionTime >= ActionClipLength)
            {
                ActionType = EPSLDinoAction::None;
                ActionWeight = 0.0f;
            }
        }
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

        if (ActionType != EPSLDinoAction::None && ActionWeight > ZERO_ANIMWEIGHT_THRESH)
        {
            ActionNodes[static_cast<int32>(ActionType) - 1].Update_AnyThread(InContext.FractionalWeight(ActionWeight));
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

        if (ActionType != EPSLDinoAction::None && ActionWeight > ZERO_ANIMWEIGHT_THRESH)
        {
            FPoseContext ActionPose(Output);
            ActionNodes[static_cast<int32>(ActionType) - 1].Evaluate_AnyThread(ActionPose);
            FAnimationPoseData OutData(Output);
            const FAnimationPoseData ActionData(ActionPose);
            FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutData, ActionData, 1.0f - ActionWeight);
        }
        return true;
    }

    virtual void PostUpdate(UAnimInstance* InAnimInstance) const override
    {
        FAnimInstanceProxy::PostUpdate(InAnimInstance);
        UPSLDinoAnimInstance* Dino = CastChecked<UPSLDinoAnimInstance>(InAnimInstance);
        Dino->ActiveAction = ActionType;
        Dino->ActionWeight = ActionWeight;
    }
};
}

UPSLDinoAnimInstance::UPSLDinoAnimInstance()
{
    SpeedInterpRate = 8.0f;
    RootMotionMode = ERootMotionMode::NoRootMotionExtraction;
    ActiveAction = EPSLDinoAction::None;
    ActionWeight = 0.0f;
    RequestedAction = EPSLDinoAction::None;
    ActionRequestId = 0;
}

FAnimInstanceProxy* UPSLDinoAnimInstance::CreateAnimInstanceProxy()
{
    return new FPSLDinoAnimProxy(this);
}
