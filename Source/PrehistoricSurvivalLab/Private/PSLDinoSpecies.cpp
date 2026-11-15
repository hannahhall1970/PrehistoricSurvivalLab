#include "PSLDinoSpecies.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Internationalization/StringTableRegistry.h"

const FName UPSLDinoSpecies::TRexId(TEXT("TRex"));
const FName UPSLDinoSpecies::ParaId(TEXT("Para"));
const FPrimaryAssetType UPSLDinoSpecies::AssetType(TEXT("PSLSpecies"));
const FName UPSLDinoSpecies::TextTableId(TEXT("PSLSpecies"));

void UPSLDinoSpecies::RegisterTextTable()
{
    // Species texts are string-table entries so DA_Species_* assets and MakeBuiltin share one identity (TableId + Key).
    // A saved string-table text stores only that reference: it is not rekeyed into the asset's package namespace and
    // not gathered from the asset. GatherText reads these macros (literal arguments required) under namespace "PSL",
    // so the existing PSL,Species* .po/.locres entries keep translating them. Translation is covered by PSL.Loc.PoComplete
    // and PSL.UI.Localization; PSL.Species.Assets deliberately does not switch culture (macOS LanguageChanged crash path).
    // Unregister first: RegisterStringTable checkf-fails on a duplicate ID (StringTableRegistry.cpp:71) if Startup reruns.
    UnregisterTextTable();
    LOCTABLE_NEW("PSLSpecies", "PSL");
    LOCTABLE_SETSTRING("PSLSpecies", "SpeciesTRexName", "Tyrannosaurus Rex");
    LOCTABLE_SETSTRING("PSLSpecies", "SpeciesTRexDesc", "Apex carnivore. Hits hard, eats meat from carcasses.");
    LOCTABLE_SETSTRING("PSLSpecies", "SpeciesParaName", "Parasaurolophus");
    LOCTABLE_SETSTRING("PSLSpecies", "SpeciesParaDesc", "Fast, agile herbivore. Grazes on plants; its bite only wounds.");
}

void UPSLDinoSpecies::UnregisterTextTable()
{
    FStringTableRegistry::Get().UnregisterStringTable(TextTableId);
}

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
        S->DisplayName = FText::FromStringTable(TextTableId, TEXT("SpeciesTRexName"));
        S->Description = FText::FromStringTable(TextTableId, TEXT("SpeciesTRexDesc"));
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
        S->DisplayName = FText::FromStringTable(TextTableId, TEXT("SpeciesParaName"));
        S->Description = FText::FromStringTable(TextTableId, TEXT("SpeciesParaDesc"));
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
