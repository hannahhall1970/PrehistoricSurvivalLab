#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "PSLCharacter.h"
#include "PSLInputDefaults.h"
#include "PSLSurvivalComponent.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    const FEnhancedActionKeyMapping* FindMapping(const UInputMappingContext* Context, const FKey& Key)
    {
        return Context->GetMappings().FindByPredicate([&Key](const FEnhancedActionKeyMapping& Mapping)
        {
            return Mapping.Key == Key;
        });
    }

    // Feeds a unit key press (1,0) through the mapping's modifiers, as Enhanced Input does for a digital key.
    FVector2D ResolveAxis2D(const FEnhancedActionKeyMapping& Mapping)
    {
        FInputActionValue Value(FVector2D(1.0, 0.0));
        for (UInputModifier* Modifier : Mapping.Modifiers)
        {
            Value = Modifier->ModifyRaw(nullptr, Value, 0.0f);
        }
        return Value.Get<FVector2D>();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLInputDefaultBindingsTest, "PSL.Input.DefaultBindings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLInputDefaultBindingsTest::RunTest(const FString& Parameters)
{
    UObject* Outer = GetTransientPackage();

    TObjectPtr<UInputMappingContext> Context;
    TObjectPtr<UInputAction> Move;
    TObjectPtr<UInputAction> Look;
    TObjectPtr<UInputAction> Jump;
    TObjectPtr<UInputAction> Sprint;
    TObjectPtr<UInputAction> Bite;
    PSLInputDefaults::Resolve(Outer, Context, Move, Look, Jump, Sprint, Bite);

    if (!TestNotNull(TEXT("Context built"), Context.Get()) || !TestNotNull(TEXT("Move built"), Move.Get())
        || !TestNotNull(TEXT("Look built"), Look.Get()) || !TestNotNull(TEXT("Jump built"), Jump.Get())
        || !TestNotNull(TEXT("Sprint built"), Sprint.Get()) || !TestNotNull(TEXT("Bite built"), Bite.Get()))
    {
        return false;
    }

    struct FMoveCase { FKey Key; FVector2D Expected; };
    const FMoveCase MoveCases[] =
    {
        { EKeys::W, FVector2D(0, 1) },  { EKeys::Up, FVector2D(0, 1) },
        { EKeys::S, FVector2D(0, -1) }, { EKeys::Down, FVector2D(0, -1) },
        { EKeys::D, FVector2D(1, 0) },  { EKeys::Right, FVector2D(1, 0) },
        { EKeys::A, FVector2D(-1, 0) }, { EKeys::Left, FVector2D(-1, 0) },
    };
    for (const FMoveCase& Case : MoveCases)
    {
        const FEnhancedActionKeyMapping* Mapping = FindMapping(Context, Case.Key);
        if (TestNotNull(*FString::Printf(TEXT("%s mapped"), *Case.Key.ToString()), Mapping))
        {
            TestTrue(*FString::Printf(TEXT("%s maps to Move"), *Case.Key.ToString()), Mapping->Action == Move);
            TestTrue(*FString::Printf(TEXT("%s direction"), *Case.Key.ToString()), ResolveAxis2D(*Mapping).Equals(Case.Expected));
        }
    }

    const FEnhancedActionKeyMapping* LookMapping = FindMapping(Context, EKeys::Mouse2D);
    if (TestNotNull(TEXT("Mouse2D mapped"), LookMapping))
    {
        TestTrue(TEXT("Mouse2D maps to Look"), LookMapping->Action == Look);
    }
    const FEnhancedActionKeyMapping* JumpMapping = FindMapping(Context, EKeys::SpaceBar);
    if (TestNotNull(TEXT("SpaceBar mapped"), JumpMapping))
    {
        TestTrue(TEXT("SpaceBar maps to Jump"), JumpMapping->Action == Jump);
    }
    const FEnhancedActionKeyMapping* SprintMapping = FindMapping(Context, EKeys::LeftShift);
    if (TestNotNull(TEXT("LeftShift mapped"), SprintMapping))
    {
        TestTrue(TEXT("LeftShift maps to Sprint"), SprintMapping->Action == Sprint);
    }
    const FEnhancedActionKeyMapping* BiteMapping = FindMapping(Context, EKeys::LeftMouseButton);
    if (TestNotNull(TEXT("LeftMouseButton mapped"), BiteMapping))
    {
        TestTrue(TEXT("LeftMouseButton maps to Bite"), BiteMapping->Action == Bite);
    }

    // An assigned asset must survive Resolve untouched, and defaults must bind to it.
    UObject* OverrideOuter = GetTransientPackage();
    UInputAction* AssignedSprint = NewObject<UInputAction>(OverrideOuter);
    TObjectPtr<UInputMappingContext> OverrideContext;
    TObjectPtr<UInputAction> OverrideMove;
    TObjectPtr<UInputAction> OverrideLook;
    TObjectPtr<UInputAction> OverrideJump;
    TObjectPtr<UInputAction> OverrideSprint = AssignedSprint;
    TObjectPtr<UInputAction> OverrideBite;
    PSLInputDefaults::Resolve(OverrideOuter, OverrideContext, OverrideMove, OverrideLook, OverrideJump, OverrideSprint, OverrideBite);
    TestTrue(TEXT("Assigned action is kept"), OverrideSprint == AssignedSprint);
    const FEnhancedActionKeyMapping* OverrideMapping = FindMapping(OverrideContext, EKeys::LeftShift);
    if (TestNotNull(TEXT("Override LeftShift mapped"), OverrideMapping))
    {
        TestTrue(TEXT("LeftShift maps to the assigned action"), OverrideMapping->Action == AssignedSprint);
    }

    UObject* KeptOuter = GetTransientPackage();
    UInputMappingContext* AssignedContext = NewObject<UInputMappingContext>(KeptOuter);
    TObjectPtr<UInputMappingContext> KeptContext = AssignedContext;
    TObjectPtr<UInputAction> NullMove;
    TObjectPtr<UInputAction> NullLook;
    TObjectPtr<UInputAction> NullJump;
    TObjectPtr<UInputAction> NullSprint;
    TObjectPtr<UInputAction> NullBite;
    PSLInputDefaults::Resolve(KeptOuter, KeptContext, NullMove, NullLook, NullJump, NullSprint, NullBite);
    TestTrue(TEXT("Assigned context is kept"), KeptContext == AssignedContext);
    TestEqual(TEXT("Assigned context gets no default mappings"), AssignedContext->GetMappings().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLCharacterSprintSpeedTest, "PSL.Character.SprintSpeed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLCharacterSprintSpeedTest::RunTest(const FString& Parameters)
{
    if (!TestNotNull(TEXT("GEngine available"), GEngine))
    {
        return false;
    }

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSLCharacter* Character = World->SpawnActor<APSLCharacter>(APSLCharacter::StaticClass(), FTransform::Identity, SpawnParams);

    // The bare test world may not auto-begin spawned actors; the sprint delegate is bound in BeginPlay.
    if (Character && !Character->HasActorBegunPlay())
    {
        Character->DispatchBeginPlay();
    }

    if (TestNotNull(TEXT("Character spawned"), Character))
    {
        UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
        UPSLSurvivalComponent* Survival = Character->GetSurvivalComponent();
        TestEqual(TEXT("Starts at walk speed"), Movement->MaxWalkSpeed, Character->WalkSpeed);

        Survival->SetSprintRequested(true);
        Survival->UpdateStamina(0.1f, true);
        TestTrue(TEXT("Sprinting while held"), Survival->IsSprinting());
        TestEqual(TEXT("Sprint speed while sprinting"), Movement->MaxWalkSpeed, Character->SprintSpeed);

        Survival->SetSprintRequested(false);
        Survival->UpdateStamina(0.1f, true);
        TestFalse(TEXT("Not sprinting after release"), Survival->IsSprinting());
        TestEqual(TEXT("Walk speed after release"), Movement->MaxWalkSpeed, Character->WalkSpeed);
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
