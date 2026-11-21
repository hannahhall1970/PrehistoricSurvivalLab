#include "PSLDinoSpecies.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "HAL/IConsoleManager.h"
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

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarDevHeroTrex(
    TEXT("psl.Dev.HeroTrex"), 0,
    TEXT("Dev only: 1 makes the built-in T-Rex species use the imported hero asset (/Game/Dev/ArtDir/Hero) when present."),
    ECVF_Default);
#endif

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
        S->HungerDrainPerSecond = 0.15f; // ~11.5 min idle to death (666.7 s to Hunger 0 + 25 s starving)
        S->FoodPerBite = 1.0f;

#if !UE_BUILD_SHIPPING
        if (CVarDevHeroTrex.GetValueOnGameThread() != 0 && FPackageName::DoesPackageExist(TEXT("/Game/Dev/ArtDir/Hero/SK_HeroTrex")))
        {
            S->Mesh = Soft<USkeletalMesh>(TEXT("/Game/Dev/ArtDir/Hero/SK_HeroTrex.SK_HeroTrex"));
            S->IdleAnim = Soft<UAnimSequence>(TEXT("/Game/Dev/ArtDir/Hero/A_HeroTrex_Idle.A_HeroTrex_Idle"));
            S->WalkAnim = Soft<UAnimSequence>(TEXT("/Game/Dev/ArtDir/Hero/A_HeroTrex_Walk.A_HeroTrex_Walk"));
            S->RunAnim = Soft<UAnimSequence>(TEXT("/Game/Dev/ArtDir/Hero/A_HeroTrex_Run.A_HeroTrex_Run"));
            S->AttackAnim = Soft<UAnimSequence>(TEXT("/Game/Dev/ArtDir/Hero/A_HeroTrex_Attack.A_HeroTrex_Attack"));
            S->DeathAnim = Soft<UAnimSequence>(TEXT("/Game/Dev/ArtDir/Hero/A_HeroTrex_Death.A_HeroTrex_Death"));
            S->AttackPlayRate = 1.5f;       // clip window 0.375..1.875 s -> 1.0 s action, fits BiteCooldown
            S->AttackStartTime = 0.375f;    // r8: keep the head pull-back (clip f11..f27); jaw < 20 deg vs idle up to press + 0.30 s
            S->AttackEndTime = 1.875f;      // clip f56: jaw shut since f48, lunge follow-through done
            S->AttackContactTime = 1.567f;  // jaw contact (clip f47, jaw closing 7.8 deg) -> BiteHitDelay 0.795 s
            S->WalkAnimSpeed = 60.0f;  // stance-phase toe contact speed per play-rate unit, mean of PSL.Dev.HeroTrex.Showcase suggested_anim_speed (L 60.1, R 60.5)
            S->RunAnimSpeed = 229.0f;  // mean of PSL.Dev.HeroTrex.Showcase run suggested_anim_speed (L 227.7, R 229.4); 245 left 40-47 uu stance slide
            S->MeshScale = 0.0f;      // fit to 90% of the capsule height
            S->MeshYaw = -90.0f;      // import axis_forward=-Y; PSL.Dev.HeroTrex.Showcase measures facing
            S->HitVfxScale = 0.35f;   // ~17 uu contact flash at the jaw/victim point
        }
#endif
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
        S->HungerDrainPerSecond = 0.12f; // ~14.2 min idle to death (833.3 s to Hunger 0 + 17.5 s starving)
        S->FoodPerBite = 0.6f;
    }
    // MeshScale stays 0: fit to 90% of the capsule.
    return S;
}
