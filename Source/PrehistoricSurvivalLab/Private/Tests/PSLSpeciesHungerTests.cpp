#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PSLCharacter.h"
#include "PSLDinoSpecies.h"
#include "PSLFoodPlant.h"
#include "PSLPreyCharacter.h"
#include "PSLStatsComponent.h"
#include "PSLSurvivalComponent.h"
#include "UObject/UObjectGlobals.h"
#include "PSLVitalsListener.h"

namespace
{
    // Bare game world; actors do not auto-begin play here, so callers dispatch BeginPlay themselves.
    struct FPSLTestWorld
    {
        FPSLTestWorld()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
            Context.SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());
            World->BeginPlay();
        }

        ~FPSLTestWorld()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        template <typename T>
        T* Spawn()
        {
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            T* Actor = World->SpawnActor<T>(T::StaticClass(), FTransform::Identity, Params);
            if (Actor && !Actor->HasActorBegunPlay())
            {
                Actor->DispatchBeginPlay();
            }
            return Actor;
        }

        UWorld* World = nullptr;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSpeciesBuiltinTest, "PSL.Species.Builtin", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSpeciesBuiltinTest::RunTest(const FString& Parameters)
{
    const UPSLDinoSpecies* TRex = UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::TRexId, GetTransientPackage());
    const UPSLDinoSpecies* Para = UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::ParaId, GetTransientPackage());
    TestNull(TEXT("Unknown id yields no species"), UPSLDinoSpecies::MakeBuiltin(FName(TEXT("Raptor")), GetTransientPackage()));
    if (!TestNotNull(TEXT("TRex built"), TRex) || !TestNotNull(TEXT("Para built"), Para))
    {
        return false;
    }

    TestTrue(TEXT("TRex id"), TRex->SpeciesId == UPSLDinoSpecies::TRexId);
    TestTrue(TEXT("TRex is a carnivore"), TRex->Diet == EPSLDiet::Carnivore);
    TestTrue(TEXT("Para is a herbivore"), Para->Diet == EPSLDiet::Herbivore);
    TestEqual(TEXT("Para max health"), Para->MaxHealth, 70.0f);
    TestEqual(TEXT("Para walk speed"), Para->WalkSpeed, 340.0f);
    TestEqual(TEXT("Para sprint speed"), Para->SprintSpeed, 760.0f);
    TestTrue(TEXT("Para is faster than the TRex"), Para->SprintSpeed > TRex->SprintSpeed);
    TestTrue(TEXT("Para bites weaker than the TRex"), Para->BiteDamage < TRex->BiteDamage);

    // The TRex species must equal the constructor defaults of the player character.
    const APSLCharacter* Defaults = GetDefault<APSLCharacter>();
    TestEqual(TEXT("TRex walk speed = character default"), TRex->WalkSpeed, Defaults->WalkSpeed);
    TestEqual(TEXT("TRex sprint speed = character default"), TRex->SprintSpeed, Defaults->SprintSpeed);
    TestEqual(TEXT("TRex bite damage = character default"), TRex->BiteDamage, Defaults->BiteDamage);
    TestEqual(TEXT("TRex bite radius = character default"), TRex->BiteRadius, Defaults->BiteRadius);
    TestEqual(TEXT("TRex bite cooldown = character default"), TRex->BiteCooldown, Defaults->BiteCooldown);
    TestEqual(TEXT("TRex capsule radius"), TRex->CapsuleRadius, APSLCharacter::DinoCapsuleRadius);
    TestEqual(TEXT("TRex capsule half height"), TRex->CapsuleHalfHeight, APSLCharacter::DinoCapsuleHalfHeight);
    TestEqual(TEXT("TRex mesh yaw"), TRex->MeshYaw, APSLCharacter::DinoMeshYaw);
    TestEqual(TEXT("TRex mesh path"), TRex->Mesh.ToSoftObjectPath().ToString(), Defaults->DinoMesh.ToSoftObjectPath().ToString());

    TestTrue(TEXT("TRex id known"), UPSLDinoSpecies::IsKnownSpeciesId(UPSLDinoSpecies::TRexId));
    TestFalse(TEXT("Empty id unknown"), UPSLDinoSpecies::IsKnownSpeciesId(NAME_None));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSpeciesApplyTest, "PSL.Species.Apply", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSpeciesApplyTest::RunTest(const FString& Parameters)
{
    if (!TestNotNull(TEXT("GEngine available"), GEngine))
    {
        return false;
    }
    FPSLTestWorld Fixture;
    APSLCharacter* Character = Fixture.Spawn<APSLCharacter>();
    if (!TestNotNull(TEXT("Character spawned"), Character))
    {
        return false;
    }
    TestNull(TEXT("No species before ApplySpecies"), Character->GetSpecies());

    const UPSLDinoSpecies* Para = UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::ParaId, Character);
    Character->ApplySpecies(nullptr);
    TestNull(TEXT("Null species is ignored"), Character->GetSpecies());
    Character->ApplySpecies(Para);

    TestTrue(TEXT("Species stored"), Character->GetSpecies() == Para);
    TestEqual(TEXT("Walk speed"), Character->WalkSpeed, Para->WalkSpeed);
    TestEqual(TEXT("Sprint speed"), Character->SprintSpeed, Para->SprintSpeed);
    TestEqual(TEXT("Movement uses the walk speed"), Character->GetCharacterMovement()->MaxWalkSpeed, Para->WalkSpeed);
    TestEqual(TEXT("Bite damage"), Character->BiteDamage, Para->BiteDamage);
    TestEqual(TEXT("Capsule radius"), Character->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), Para->CapsuleRadius);
    TestEqual(TEXT("Capsule half height"), Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), Para->CapsuleHalfHeight);
    TestEqual(TEXT("Max health"), Character->GetStatsComponent()->GetMaxHealth(), Para->MaxHealth);
    TestEqual(TEXT("Health starts full"), Character->GetStatsComponent()->GetHealth(), Para->MaxHealth);
    TestEqual(TEXT("Max stamina"), Character->GetSurvivalComponent()->GetMaxStamina(), Para->MaxStamina);
    TestEqual(TEXT("Hunger starts full"), Character->GetSurvivalComponent()->GetHunger(), Para->MaxHunger);
    TestEqual(TEXT("Hunger drain"), Character->GetSurvivalComponent()->HungerDrainPerSecond, Para->HungerDrainPerSecond);
    TestEqual(TEXT("Food multiplier"), Character->FoodPerBite, Para->FoodPerBite);
    TestEqual(TEXT("Mesh path follows the species"), Character->DinoMesh.ToSoftObjectPath().ToString(), Para->Mesh.ToSoftObjectPath().ToString());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLHungerDrainAndStarveTest, "PSL.Hunger.DrainAndStarve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLHungerDrainAndStarveTest::RunTest(const FString& Parameters)
{
    if (!TestNotNull(TEXT("GEngine available"), GEngine))
    {
        return false;
    }
    FPSLTestWorld Fixture;
    APSLCharacter* Character = Fixture.Spawn<APSLCharacter>();
    if (!TestNotNull(TEXT("Character spawned"), Character))
    {
        return false;
    }
    UPSLSurvivalComponent* Survival = Character->GetSurvivalComponent();
    UPSLStatsComponent* Stats = Character->GetStatsComponent();
    Survival->MaxHunger = 100.0f;
    Survival->Hunger = 100.0f;
    Survival->HungerDrainPerSecond = 1.0f;
    Survival->StarveDamagePerSecond = 10.0f;

    Survival->UpdateHunger(10.0f);
    TestEqual(TEXT("Idle drain"), Survival->GetHunger(), 90.0f);

    Survival->SetSprintRequested(true);
    Survival->UpdateStamina(0.1f, true);
    TestTrue(TEXT("Sprinting"), Survival->IsSprinting());
    Survival->UpdateHunger(10.0f);
    TestEqual(TEXT("Sprint drains twice as fast"), Survival->GetHunger(), 70.0f);
    Survival->SetSprintRequested(false);
    Survival->UpdateStamina(0.1f, true);

    Survival->AddFood(500.0f);
    TestEqual(TEXT("Food is capped at max"), Survival->GetHunger(), 100.0f);
    Survival->AddFood(-5.0f);
    TestEqual(TEXT("Non-positive food ignored"), Survival->GetHunger(), 100.0f);

    Survival->Hunger = 0.0f;
    TestTrue(TEXT("Starving at zero"), Survival->IsStarving());
    const float HealthBefore = Stats->GetHealth();
    Survival->UpdateHunger(1.0f);
    TestEqual(TEXT("Starvation damages health"), Stats->GetHealth(), HealthBefore - 10.0f);

    for (int32 Step = 0; Step < 20 && !Stats->IsDead(); ++Step)
    {
        Survival->UpdateHunger(1.0f);
    }
    TestTrue(TEXT("Starvation kills"), Stats->IsDead());
    TestEqual(TEXT("Health is zero"), Stats->GetHealth(), 0.0f);

    Survival->Hunger = 50.0f;
    Survival->UpdateHunger(5.0f);
    TestEqual(TEXT("No drain after death"), Survival->GetHunger(), 50.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLHungerDietTest, "PSL.Hunger.Diet", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLHungerDietTest::RunTest(const FString& Parameters)
{
    if (!TestNotNull(TEXT("GEngine available"), GEngine))
    {
        return false;
    }
    FPSLTestWorld Fixture;

    APSLPreyCharacter* Prey = Fixture.Spawn<APSLPreyCharacter>();
    if (!TestNotNull(TEXT("Prey spawned"), Prey))
    {
        return false;
    }
    TestEqual(TEXT("A live prey cannot be eaten"), Prey->EatMeat(25.0f), 0.0f);
    Prey->GetStatsComponent()->ApplyDamage(1000.0f);
    TestTrue(TEXT("Prey is dead"), Prey->IsDead());
    TestEqual(TEXT("A bite takes 25 meat"), Prey->EatMeat(25.0f), 25.0f);
    TestEqual(TEXT("Meat left"), Prey->MeatRemaining, 35.0f);
    TestEqual(TEXT("Zero request takes nothing"), Prey->EatMeat(0.0f), 0.0f);
    TestEqual(TEXT("Last bite is clamped to the meat left"), Prey->EatMeat(100.0f), 35.0f);
    TestEqual(TEXT("Nothing left"), Prey->EatMeat(25.0f), 0.0f);
    TestTrue(TEXT("Carcass scheduled for removal"), Prey->GetLifeSpan() > 0.0f);

    APSLFoodPlant* Plant = Fixture.Spawn<APSLFoodPlant>();
    if (TestNotNull(TEXT("Plant spawned"), Plant))
    {
        TestTrue(TEXT("Plant starts available"), Plant->IsAvailable());
        TestEqual(TEXT("Plant yields its food"), Plant->Consume(), Plant->FoodAmount);
        TestFalse(TEXT("Plant is hidden while regrowing"), Plant->IsAvailable());
        TestEqual(TEXT("A regrowing plant yields nothing"), Plant->Consume(), 0.0f);
    }

    const UPSLDinoSpecies* TRex = UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::TRexId, GetTransientPackage());
    const UPSLDinoSpecies* Para = UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::ParaId, GetTransientPackage());
    TestTrue(TEXT("TRex eats meat"), TRex->Diet == EPSLDiet::Carnivore);
    TestTrue(TEXT("Para eats plants"), Para->Diet == EPSLDiet::Herbivore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSurvivalRestoreTest, "PSL.Hunger.RestoreSurvivesBeginPlay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSurvivalRestoreTest::RunTest(const FString& Parameters)
{
    UPSLSurvivalComponent* Fresh = NewObject<UPSLSurvivalComponent>();
    Fresh->Stamina = 10.0f;
    Fresh->Hunger = 10.0f;
    Fresh->ResetVitalsForPlay();
    TestEqual(TEXT("Without a restore BeginPlay fills stamina"), Fresh->GetStamina(), Fresh->GetMaxStamina());
    TestEqual(TEXT("Without a restore BeginPlay fills hunger"), Fresh->GetHunger(), Fresh->GetMaxHunger());

    UPSLSurvivalComponent* Restored = NewObject<UPSLSurvivalComponent>();
    Restored->RestoreState(33.0f, 44.0f);
    Restored->ResetVitalsForPlay();
    TestEqual(TEXT("Restored stamina survives BeginPlay"), Restored->GetStamina(), 33.0f);
    TestEqual(TEXT("Restored hunger survives BeginPlay"), Restored->GetHunger(), 44.0f);

    Restored->RestoreState(5000.0f, -3.0f);
    TestEqual(TEXT("Stamina clamped to max"), Restored->GetStamina(), Restored->GetMaxStamina());
    TestEqual(TEXT("Hunger clamped to zero"), Restored->GetHunger(), 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLRestoreBroadcastsTest, "PSL.Hunger.RestoreBroadcasts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLRestoreBroadcastsTest::RunTest(const FString& Parameters)
{
    // A loaded save must reach the HUD through the normal change events, not only through the next poll.
    UPSLVitalsListener* Listener = NewObject<UPSLVitalsListener>();

    UPSLSurvivalComponent* Survival = NewObject<UPSLSurvivalComponent>();
    Survival->OnStaminaChanged.AddDynamic(Listener, &UPSLVitalsListener::OnStamina);
    Survival->OnHungerChanged.AddDynamic(Listener, &UPSLVitalsListener::OnHunger);
    Survival->RestoreState(33.0f, 44.0f);
    TestEqual(TEXT("Stamina broadcast once"), Listener->StaminaCalls, 1);
    TestEqual(TEXT("Stamina broadcast carries the restored value"), Listener->LastStamina, 33.0f);
    TestEqual(TEXT("Hunger broadcast once"), Listener->HungerCalls, 1);
    TestEqual(TEXT("Hunger broadcast carries the restored value"), Listener->LastHunger, 44.0f);

    Survival->RestoreState(5000.0f, -3.0f);
    TestEqual(TEXT("Broadcast stamina is the clamped value"), Listener->LastStamina, Survival->GetMaxStamina());
    TestEqual(TEXT("Broadcast hunger is the clamped value"), Listener->LastHunger, 0.0f);

    UPSLStatsComponent* Stats = NewObject<UPSLStatsComponent>();
    Stats->OnHealthChanged.AddDynamic(Listener, &UPSLVitalsListener::OnHealth);
    const float Full = Stats->GetHealth();
    Stats->SetHealth(37.0f);
    TestEqual(TEXT("Health restored"), Stats->GetHealth(), 37.0f);
    TestEqual(TEXT("Health broadcast once"), Listener->HealthCalls, 1);
    TestEqual(TEXT("Health broadcast carries the value"), Listener->LastHealth, 37.0f);
    TestEqual(TEXT("Health broadcast carries the delta"), Listener->LastHealthDelta, 37.0f - Full);
    Stats->SetHealth(37.0f);
    TestEqual(TEXT("Unchanged health does not broadcast"), Listener->HealthCalls, 1);
    Stats->SetHealth(99999.0f);
    TestEqual(TEXT("Health clamps to max"), Stats->GetHealth(), Stats->GetMaxHealth());
    Stats->SetHealth(-5.0f);
    TestEqual(TEXT("Health clamps to zero"), Stats->GetHealth(), 0.0f);
    TestFalse(TEXT("SetHealth never marks the owner dead"), Stats->IsDead());
    return true;
}

#endif
