#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PSLLevelBuildLibrary.generated.h"

class ALandscape;
class AActor;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/** Editor-time level authoring helpers, called from scripts/build_lost_valley.py (Python cannot reach ALandscapeProxy::Import). */
UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLLevelBuildLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Landscape quads per subsection (1 subsection per component), so SizeVerts must be 63 * N + 1.
    static constexpr int32 SubsectionQuads = 63;

    // Heights are uint16 landscape texture values: 32768 = local z 0, world z = (H - 32768) / 128 * Scale.Z.
    // Returns nullptr (and logs an error) on invalid input, or outside the editor (WITH_EDITOR only).
    UFUNCTION(BlueprintCallable, Category="PSL|LevelBuild", meta=(WorldContext="WorldContext"))
    static ALandscape* CreateLandscape(UObject* WorldContext, FVector Location, FVector Scale, int32 SizeVerts, const TArray<int32>& Heights, UMaterialInterface* Material);

    // Adds one HISM component with the given instance transforms (relative to Owner's root) to Owner.
    // CullDistance <= 0 disables culling. Returns nullptr (and logs an error) on invalid input.
    UFUNCTION(BlueprintCallable, Category="PSL|LevelBuild")
    static UHierarchicalInstancedStaticMeshComponent* AddMeshInstances(AActor* Owner, UStaticMesh* Mesh, const TArray<FTransform>& Transforms, bool bCollision, float CullDistance);
};
