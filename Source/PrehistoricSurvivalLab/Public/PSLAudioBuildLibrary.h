#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PSLAudioBuildLibrary.generated.h"

class USoundClass;

/** Audio authoring helpers, called from the audio generator script (USoundClass::SetParentClass is WITH_EDITOR-only and not script-exposed). */
UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLAudioBuildLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Re-parents Child under Parent (nullptr = detach to root), keeping ParentClass and ChildClasses consistent. Idempotent.
    // Returns false (logs an error, hierarchy untouched) when Child is null, Parent == Child, or Parent descends from Child (cycle).
    UFUNCTION(BlueprintCallable, Category="PSL|AudioBuild")
    static bool SetSoundClassParent(USoundClass* Child, USoundClass* Parent);
};
