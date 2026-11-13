#include "PSLLevelBuildLibrary.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

#if WITH_EDITOR
#include "Landscape.h"
#include "LandscapeProxy.h"
#endif

ALandscape* UPSLLevelBuildLibrary::CreateLandscape(UObject* WorldContext, FVector Location, FVector Scale, int32 SizeVerts, const TArray<int32>& Heights, UMaterialInterface* Material)
{
#if WITH_EDITOR
    UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("PSL CreateLandscape: no world from WorldContext"));
        return nullptr;
    }
    if (SizeVerts < SubsectionQuads + 1 || (SizeVerts - 1) % SubsectionQuads != 0)
    {
        UE_LOG(LogTemp, Error, TEXT("PSL CreateLandscape: SizeVerts %d is not 63 * N + 1"), SizeVerts);
        return nullptr;
    }
    if (Heights.Num() != SizeVerts * SizeVerts)
    {
        UE_LOG(LogTemp, Error, TEXT("PSL CreateLandscape: %d heights, expected %d"), Heights.Num(), SizeVerts * SizeVerts);
        return nullptr;
    }

    TArray<uint16> HeightData;
    HeightData.Reserve(Heights.Num());
    for (const int32 H : Heights)
    {
        if (H < 0 || H > 65535)
        {
            UE_LOG(LogTemp, Error, TEXT("PSL CreateLandscape: height %d outside 0..65535"), H);
            return nullptr;
        }
        HeightData.Add(static_cast<uint16>(H));
    }

    // ALandscape's constructor sets root scale (128,128,256); the default MultiplyWithRoot would multiply it.
    FActorSpawnParameters SpawnParams;
    SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
    ALandscape* Landscape = World->SpawnActor<ALandscape>(ALandscape::StaticClass(), FTransform(FRotator::ZeroRotator, Location, Scale), SpawnParams);
    if (!Landscape)
    {
        UE_LOG(LogTemp, Error, TEXT("PSL CreateLandscape: SpawnActor<ALandscape> failed"));
        return nullptr;
    }

    TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
    HeightDataPerLayer.Add(FGuid(), MoveTemp(HeightData));
    TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;
    MaterialLayerDataPerLayer.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

    // Same call sequence as Engine/Plugins/PCG/Source/PCG/Private/Tests/PCGTestsCommon.cpp.
    Landscape->Import(FGuid::NewGuid(), 0, 0, SizeVerts - 1, SizeVerts - 1, 1, SubsectionQuads,
        HeightDataPerLayer, nullptr, MaterialLayerDataPerLayer, ELandscapeImportAlphamapType::Additive,
        MakeArrayView<const FLandscapeLayer>({}));
    Landscape->CreateLandscapeInfo();

    if (Material)
    {
        Landscape->LandscapeMaterial = Material;
        Landscape->PostEditChange();
    }
    return Landscape;
#else
    UE_LOG(LogTemp, Error, TEXT("PSL CreateLandscape is editor-only"));
    return nullptr;
#endif
}

UHierarchicalInstancedStaticMeshComponent* UPSLLevelBuildLibrary::AddMeshInstances(AActor* Owner, UStaticMesh* Mesh, const TArray<FTransform>& Transforms, bool bCollision, float CullDistance)
{
    if (!Owner || !Mesh || Transforms.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("PSL AddMeshInstances: invalid input (owner=%s mesh=%s transforms=%d)"),
            *GetNameSafe(Owner), *GetNameSafe(Mesh), Transforms.Num());
        return nullptr;
    }

    UHierarchicalInstancedStaticMeshComponent* Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner, NAME_None, RF_Transactional);
    Component->SetStaticMesh(Mesh);
    Component->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
    if (CullDistance > 0.0f)
    {
        Component->InstanceEndCullDistance = static_cast<int32>(CullDistance);
    }

    if (USceneComponent* Root = Owner->GetRootComponent())
    {
        Component->SetupAttachment(Root);
    }
    else
    {
        Owner->SetRootComponent(Component);
    }
    Owner->AddInstanceComponent(Component);
    Component->RegisterComponent();
    Component->AddInstances(Transforms, false, false);
    return Component;
}
