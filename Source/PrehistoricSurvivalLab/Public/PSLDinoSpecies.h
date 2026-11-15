#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PSLDinoSpecies.generated.h"

class UAnimSequence;
class USkeletalMesh;

UENUM(BlueprintType)
enum class EPSLDiet : uint8
{
    Carnivore,
    Herbivore
};

/** Playable species definition. Built-in values are code-constructible so tests do not depend on assets. */
UCLASS(BlueprintType)
class PREHISTORICSURVIVALLAB_API UPSLDinoSpecies : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    static const FName TRexId;
    static const FName ParaId;

    // Asset Manager type used in DefaultGame.ini (directory /Game/Data/Species).
    static const FPrimaryAssetType AssetType;

    // String table "PSLSpecies" (namespace PSL) holding DisplayName/Description of both species.
    // Registered by the game module on startup; MakeBuiltin and DA_Species_* reference its entries.
    static const FName TextTableId;
    static void RegisterTextTable();
    static void UnregisterTextTable();

    // Id is TRexId or ParaId; anything else returns nullptr.
    static UPSLDinoSpecies* MakeBuiltin(FName Id, UObject* Outer);

    static bool IsKnownSpeciesId(FName Id) { return Id == TRexId || Id == ParaId; }

    virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(AssetType, SpeciesId); }

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Species")
    FName SpeciesId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Species")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Species")
    FText Description;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Species")
    EPSLDiet Diet = EPSLDiet::Carnivore;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual")
    TSoftObjectPtr<USkeletalMesh> Mesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual")
    TSoftObjectPtr<UAnimSequence> IdleAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual")
    TSoftObjectPtr<UAnimSequence> WalkAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual")
    TSoftObjectPtr<UAnimSequence> RunAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual")
    TSoftObjectPtr<UAnimSequence> AttackAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual")
    TSoftObjectPtr<UAnimSequence> DeathAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Body")
    float CapsuleRadius = 60.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Body")
    float CapsuleHalfHeight = 120.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Body")
    float MeshYaw = -90.0f;

    // 0 = fit the mesh to 90% of the capsule height (same rule as APSLPreyCharacter).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Body")
    float MeshScale = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats")
    float MaxHealth = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats")
    float MaxStamina = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats")
    float WalkSpeed = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats")
    float SprintSpeed = 600.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat")
    float BiteDamage = 25.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat")
    float BiteRadius = 120.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat")
    float BiteCooldown = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hunger")
    float MaxHunger = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hunger")
    float HungerDrainPerSecond = 0.55f;

    // Multiplier on the base food a bite yields.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hunger")
    float FoodPerBite = 1.0f;
};
