#include "PSLFontBuildLibrary.h"

#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"

bool UPSLFontBuildLibrary::SetRuntimeTypefaces(UFont* Font, const TArray<FName>& Names, const TArray<UFontFace*>& Faces)
{
    if (!Font)
    {
        UE_LOG(LogTemp, Error, TEXT("PSL SetRuntimeTypefaces: null font"));
        return false;
    }
    if (Names.Num() == 0 || Names.Num() != Faces.Num())
    {
        UE_LOG(LogTemp, Error, TEXT("PSL SetRuntimeTypefaces: %d names and %d faces, expected equal non-zero counts"), Names.Num(), Faces.Num());
        return false;
    }
    TSet<FName> Seen;
    for (int32 i = 0; i < Names.Num(); ++i)
    {
        if (!Faces[i])
        {
            UE_LOG(LogTemp, Error, TEXT("PSL SetRuntimeTypefaces: null face at index %d"), i);
            return false;
        }
        if (Names[i].IsNone())
        {
            UE_LOG(LogTemp, Error, TEXT("PSL SetRuntimeTypefaces: name at index %d is None"), i);
            return false;
        }
        bool bAlreadyInSet = false;
        Seen.Add(Names[i], &bAlreadyInSet);
        if (bAlreadyInSet)
        {
            UE_LOG(LogTemp, Error, TEXT("PSL SetRuntimeTypefaces: duplicate name %s"), *Names[i].ToString());
            return false;
        }
    }

    Font->FontCacheType = EFontCacheType::Runtime;
    Font->RuntimeFontSource = ERuntimeFontSource::Asset;
    FCompositeFont& Composite = Font->GetMutableInternalCompositeFont();
    Composite.DefaultTypeface.Fonts.Empty(Names.Num());
    for (int32 i = 0; i < Names.Num(); ++i)
    {
        FTypefaceEntry& Entry = Composite.DefaultTypeface.Fonts.AddDefaulted_GetRef();
        Entry.Name = Names[i];
        Entry.Font = FFontData(Faces[i]);
    }
    Composite.FallbackTypeface.Typeface.Fonts.Empty();
    Composite.SubTypefaces.Empty();
#if WITH_EDITORONLY_DATA
    Composite.MakeDirty();
#endif
    Font->MarkPackageDirty();
    return true;
}

bool UPSLFontBuildLibrary::GetRuntimeTypefaces(const UFont* Font, TArray<FName>& OutNames, TArray<UFontFace*>& OutFaces)
{
    OutNames.Reset();
    OutFaces.Reset();
    if (!Font || Font->FontCacheType != EFontCacheType::Runtime)
    {
        return false;
    }
    for (const FTypefaceEntry& Entry : Font->GetInternalCompositeFont().DefaultTypeface.Fonts)
    {
        OutNames.Add(Entry.Name);
        OutFaces.Add(const_cast<UFontFace*>(Cast<UFontFace>(Entry.Font.GetFontFaceAsset())));
    }
    return true;
}
