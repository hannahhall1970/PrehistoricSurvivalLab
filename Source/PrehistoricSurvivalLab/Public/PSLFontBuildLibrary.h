#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PSLFontBuildLibrary.generated.h"

class UFont;
class UFontFace;

/** Font authoring helpers, called from scripts/build_game_shell.py (Python cannot set FCompositeFont typefaces or UFont::FontCacheType). */
UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLFontBuildLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Makes Font a Runtime-cache font whose DefaultTypeface is exactly {Names[i] -> Faces[i]} in order; fallback and sub typefaces are cleared.
    // Returns false (logs an error, Font untouched) unless Font is non-null, Names/Faces are equal-length and non-empty, no face is null, and names are unique and not None.
    UFUNCTION(BlueprintCallable, Category="PSL|FontBuild")
    static bool SetRuntimeTypefaces(UFont* Font, const TArray<FName>& Names, const TArray<UFontFace*>& Faces);

    // Returns false unless Font is Runtime cache type; otherwise fills the DefaultTypeface entry names and face assets in order.
    UFUNCTION(BlueprintCallable, Category="PSL|FontBuild")
    static bool GetRuntimeTypefaces(const UFont* Font, TArray<FName>& OutNames, TArray<UFontFace*>& OutFaces);
};
