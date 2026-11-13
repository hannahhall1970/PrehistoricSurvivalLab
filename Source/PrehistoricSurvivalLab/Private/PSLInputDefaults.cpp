#include "PSLInputDefaults.h"

#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace
{
    UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType ValueType)
    {
        UInputAction* Action = NewObject<UInputAction>(Outer, Name, RF_Transient);
        Action->ValueType = ValueType;
        return Action;
    }

    // Key value arrives as (1,0); swizzle moves it to Y, negate flips the chosen axes.
    void MapAxis2D(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key, bool bSwizzleToY, bool bNegateX, bool bNegateY)
    {
        FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
        if (bSwizzleToY)
        {
            UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Context);
            Swizzle->Order = EInputAxisSwizzle::YXZ;
            Mapping.Modifiers.Add(Swizzle);
        }
        if (bNegateX || bNegateY)
        {
            UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(Context);
            Negate->bX = bNegateX;
            Negate->bY = bNegateY;
            Negate->bZ = false;
            Mapping.Modifiers.Add(Negate);
        }
    }
}

void PSLInputDefaults::Resolve(
    UObject* Outer,
    TObjectPtr<UInputMappingContext>& Context,
    TObjectPtr<UInputAction>& Move,
    TObjectPtr<UInputAction>& Look,
    TObjectPtr<UInputAction>& Jump,
    TObjectPtr<UInputAction>& Sprint,
    TObjectPtr<UInputAction>& Bite,
    TObjectPtr<UInputAction>& Pause)
{
    if (!Move)
    {
        Move = MakeAction(Outer, TEXT("PSL_DefaultMove"), EInputActionValueType::Axis2D);
    }
    if (!Look)
    {
        Look = MakeAction(Outer, TEXT("PSL_DefaultLook"), EInputActionValueType::Axis2D);
    }
    if (!Jump)
    {
        Jump = MakeAction(Outer, TEXT("PSL_DefaultJump"), EInputActionValueType::Boolean);
    }
    if (!Sprint)
    {
        Sprint = MakeAction(Outer, TEXT("PSL_DefaultSprint"), EInputActionValueType::Boolean);
    }
    if (!Bite)
    {
        Bite = MakeAction(Outer, TEXT("PSL_DefaultBite"), EInputActionValueType::Boolean);
    }
    if (!Pause)
    {
        Pause = MakeAction(Outer, TEXT("PSL_DefaultPause"), EInputActionValueType::Boolean);
        Pause->bTriggerWhenPaused = true;
    }
    if (Context)
    {
        return;
    }

    Context = NewObject<UInputMappingContext>(Outer, TEXT("PSL_DefaultMappingContext"), RF_Transient);

    //                                   swizzle  -X     -Y
    MapAxis2D(Context, Move, EKeys::W,     true,  false, false);
    MapAxis2D(Context, Move, EKeys::Up,    true,  false, false);
    MapAxis2D(Context, Move, EKeys::S,     true,  false, true);
    MapAxis2D(Context, Move, EKeys::Down,  true,  false, true);
    MapAxis2D(Context, Move, EKeys::D,     false, false, false);
    MapAxis2D(Context, Move, EKeys::Right, false, false, false);
    MapAxis2D(Context, Move, EKeys::A,     false, true,  false);
    MapAxis2D(Context, Move, EKeys::Left,  false, true,  false);

    // Mouse Y is positive when moving up; AddControllerPitchInput treats positive as look down.
    MapAxis2D(Context, Look, EKeys::Mouse2D, false, false, true);

    Context->MapKey(Jump, EKeys::SpaceBar);
    Context->MapKey(Sprint, EKeys::LeftShift);
    Context->MapKey(Bite, EKeys::LeftMouseButton);
    Context->MapKey(Pause, EKeys::Escape);
    Context->MapKey(Pause, EKeys::P);
}
