#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Misc/AutomationTest.h"
#include "PSLFontBuildLibrary.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLFontBuildLibraryTest, "PSL.Fonts.BuildLibrary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLFontBuildLibraryTest::RunTest(const FString& Parameters)
{
    TArray<UFontFace*> Faces;
    for (int32 i = 0; i < 3; ++i)
    {
        Faces.Add(NewObject<UFontFace>(GetTransientPackage(), NAME_None, RF_Transient));
    }
    UFont* Font = NewObject<UFont>(GetTransientPackage(), NAME_None, RF_Transient);
    if (!TestNotNull(TEXT("font"), Font) || !TestNotNull(TEXT("face"), Faces[0]) || !TestNotNull(TEXT("face"), Faces[1]) || !TestNotNull(TEXT("face"), Faces[2]))
    {
        return false;
    }
    const TArray<FName> Names = {FName("Regular"), FName("SemiBold"), FName("Bold")};

    TestTrue(TEXT("set valid"), UPSLFontBuildLibrary::SetRuntimeTypefaces(Font, Names, Faces));
    TestTrue(TEXT("cache type"), Font->FontCacheType == EFontCacheType::Runtime);

    const auto ExpectUnchanged = [this, Font, &Names, &Faces](const TCHAR* What)
    {
        TArray<FName> GotNames;
        TArray<UFontFace*> GotFaces;
        TestTrue(FString::Printf(TEXT("%s: still runtime"), What), UPSLFontBuildLibrary::GetRuntimeTypefaces(Font, GotNames, GotFaces));
        TestTrue(FString::Printf(TEXT("%s: names unchanged"), What), GotNames == Names);
        TestTrue(FString::Printf(TEXT("%s: faces unchanged"), What), GotFaces == Faces);
    };
    ExpectUnchanged(TEXT("after set"));

    // Each invalid call logs exactly one error and must not touch the existing configuration.
    const TArray<FName> TwoNames = {FName("A"), FName("B")};
    AddExpectedError(TEXT("PSL SetRuntimeTypefaces: 2 names and 3 faces"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("mismatched lengths"), UPSLFontBuildLibrary::SetRuntimeTypefaces(Font, TwoNames, Faces));
    ExpectUnchanged(TEXT("mismatch"));

    TArray<UFontFace*> WithNull = Faces;
    WithNull[1] = nullptr;
    AddExpectedError(TEXT("PSL SetRuntimeTypefaces: null face at index 1"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("null face"), UPSLFontBuildLibrary::SetRuntimeTypefaces(Font, Names, WithNull));
    ExpectUnchanged(TEXT("null face"));

    const TArray<FName> DupNames = {FName("Regular"), FName("Bold"), FName("Regular")};
    AddExpectedError(TEXT("PSL SetRuntimeTypefaces: duplicate name Regular"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("duplicate names"), UPSLFontBuildLibrary::SetRuntimeTypefaces(Font, DupNames, Faces));
    ExpectUnchanged(TEXT("duplicate"));

    AddExpectedError(TEXT("PSL SetRuntimeTypefaces: 0 names and 0 faces"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("empty"), UPSLFontBuildLibrary::SetRuntimeTypefaces(Font, TArray<FName>(), TArray<UFontFace*>()));
    ExpectUnchanged(TEXT("empty"));

    AddExpectedError(TEXT("PSL SetRuntimeTypefaces: null font"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("null font"), UPSLFontBuildLibrary::SetRuntimeTypefaces(nullptr, Names, Faces));

    // Offline-cache fonts are not reported as runtime typefaces.
    UFont* Offline = NewObject<UFont>(GetTransientPackage(), NAME_None, RF_Transient);
    Offline->FontCacheType = EFontCacheType::Offline;
    TArray<FName> OffNames;
    TArray<UFontFace*> OffFaces;
    TestFalse(TEXT("offline font"), UPSLFontBuildLibrary::GetRuntimeTypefaces(Offline, OffNames, OffFaces));
    return true;
}
