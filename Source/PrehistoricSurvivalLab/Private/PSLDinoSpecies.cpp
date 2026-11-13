#include "PSLDinoSpecies.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"

#define LOCTEXT_NAMESPACE "PSL"

const FName UPSLDinoSpecies::TRexId(TEXT("TRex"));
const FName UPSLDinoSpecies::ParaId(TEXT("Para"));
const FPrimaryAssetType UPSLDinoSpecies::AssetType(TEXT("PSLSpecies"));

namespace
{
    template <typename T>
    TSoftObjectPtr<T> Soft(const TCHAR* Path)
    {
        return TSoftObjectPtr<T>(FSoftObjectPath(Path));
    }
}

UPSLDinoSpecies* UPSLDinoSpecies::MakeBuiltin(FName Id, UObject* Outer)
{
    if (!IsKnownSpeciesId(Id))
    {
        return nullptr;
    }

    UPSLDinoSpecies* S = NewObject<UPSLDinoSpecies>(Outer ? Outer : GetTransientPackage(), NAME_None, RF_Transient);
    S->SpeciesId = Id;
    if (Id == TRexId)
    {
        S->DisplayName = LOCTEXT("SpeciesTRexName", "Tyrannosaurus Rex");
        S->Description = LOCTEXT("SpeciesTRexDesc", "Apex carnivore. Hits hard, eats meat from carcasses.");
        S->Diet = EPSLDiet::Carnivore;
        S->Mesh = Soft<USkeletalMesh>(TEXT("/Game/Characters/Dino/SK_Trex.SK_Trex"));
        S->IdleAnim = Soft<UAnimSequence>(TEXT("/Game/Characters/Dino/A_Trex_Idle.A_Trex_Idle"));
        S->WalkAnim = Soft<UAnimSequence>(TEXT("/Game/Characters/Dino/A_Trex_Walk.A_Trex_Walk"));
        S->RunAnim = Soft<UAnimSequence>(TEXT("/Game/Characters/Dino/A_Trex_Run.A_Trex_Run"));
        S->AttackAnim = Soft<UAnimSequence>(TEXT("/Game/Characters/Dino/TrexArmature_TRex_Attack.TrexArmature_TRex_Attack"));
        S->DeathAnim = Soft<UAnimSequence>(TEXT("/Game/Characters/Dino/TrexArmature_TRex_Death.TrexArmature_TRex_Death"));
        S->CapsuleRadius = 60.0f;
        S->CapsuleHalfHeight = 120.0f;
        S->MeshYaw = -90.0f;
        S->MeshScale = 1.0f; // today's T-Rex placement: the import scale already fits the capsule
        S->MaxHealth = 100.0f;
        S->MaxStamina = 100.0f;
        S->WalkSpeed = 300.0f;
        S->SprintSpeed = 600.0f;
        S->BiteDamage = 25.0f;
        S->BiteRadius = 120.0f;
        S->BiteCooldown = 1.0f;
        S->MaxHunger = 100.0f;
        S->HungerDrainPerSecond = 0.55f;
        S->FoodPerBite = 1.0f;
    }
    else
    {
        S->DisplayName = LOCTEXT("SpeciesParaName", "Parasaurolophus");
        S->Description = LOCTEXT("SpeciesParaDesc", "Fast, agile herbivore. Grazes on plants; its bite only wounds.");
        S->Diet = EPSLDiet::Herbivore;
        S->Mesh = Soft<USkeletalMesh>(TEXT("/Game/Characters/Prey/SK_Parasaurolophus.SK_Parasaurolophus"));
        S->IdleAnim = Soft<UAnimSequence>(TEXT("/Game/Characters/Prey/A_Para_Idle.A_Para_Idle"));
        S->WalkAnim = Soft<UAnimSequence>(TEXT("/Game/Characters/Prey/A_Para_Walk.A_Para_Walk"));
        S->RunAnim = Soft<UAnimSequence>(TEXT("/Game/Characters/Prey/A_Para_Run.A_Para_Run"));
        S->AttackAnim = Soft<UAnimSequence>(TEXT("/Game/Characters/Prey/A_Para_Attack.A_Para_Attack"));
        S->DeathAnim = Soft<UAnimSequence>(TEXT("/Game/Characters/Prey/A_Para_Death.A_Para_Death"));
        S->CapsuleRadius = 50.0f;
        S->CapsuleHalfHeight = 110.0f;
        S->MeshYaw = -90.0f; // same yaw APSLPreyCharacter::ApplyPreyVisuals uses
        S->MaxHealth = 70.0f;
        S->MaxStamina = 140.0f;
        S->WalkSpeed = 340.0f;
        S->SprintSpeed = 760.0f;
        S->BiteDamage = 12.0f;
        S->BiteRadius = 100.0f;
        S->BiteCooldown = 0.7f;
        S->MaxHunger = 100.0f;
        S->HungerDrainPerSecond = 0.40f;
        S->FoodPerBite = 0.6f;
    }
    // MeshScale stays 0: fit to 90% of the capsule.
    return S;
}

#undef LOCTEXT_NAMESPACE
