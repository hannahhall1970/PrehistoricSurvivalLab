#include "Misc/AutomationTest.h"
#include "PSLAudioBuildLibrary.h"
#include "Sound/SoundClass.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLAudioBuildLibraryTest, "PSL.Audio.BuildLibrary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLAudioBuildLibraryTest::RunTest(const FString& Parameters)
{
    const auto Make = []() { return NewObject<USoundClass>(GetTransientPackage(), NAME_None, RF_Transient); };
    USoundClass* Master = Make();
    USoundClass* Music = Make();
    USoundClass* Sfx = Make();
    USoundClass* Other = Make();

    // Both sides of the link must agree: ParentClass on the child, exactly one entry in the parent's ChildClasses.
    const auto ExpectParent = [this](const TCHAR* What, USoundClass* Child, USoundClass* Parent)
    {
        TestTrue(FString::Printf(TEXT("%s: parent"), What), Child->ParentClass == Parent);
        if (Parent)
        {
            TestEqual(FString::Printf(TEXT("%s: listed once"), What), Parent->ChildClasses.FilterByPredicate([Child](const USoundClass* C) { return C == Child; }).Num(), 1);
        }
    };

    TestTrue(TEXT("music under master"), UPSLAudioBuildLibrary::SetSoundClassParent(Music, Master));
    TestTrue(TEXT("sfx under master"), UPSLAudioBuildLibrary::SetSoundClassParent(Sfx, Master));
    TestTrue(TEXT("idempotent"), UPSLAudioBuildLibrary::SetSoundClassParent(Music, Master));
    ExpectParent(TEXT("music"), Music, Master);
    ExpectParent(TEXT("sfx"), Sfx, Master);
    TestEqual(TEXT("master children"), Master->ChildClasses.Num(), 2);

    // Reparenting removes the child from the old parent.
    TestTrue(TEXT("music under other"), UPSLAudioBuildLibrary::SetSoundClassParent(Music, Other));
    ExpectParent(TEXT("moved"), Music, Other);
    TestFalse(TEXT("old parent dropped child"), Master->ChildClasses.Contains(Music));
    TestTrue(TEXT("music back under master"), UPSLAudioBuildLibrary::SetSoundClassParent(Music, Master));
    TestFalse(TEXT("other dropped child"), Other->ChildClasses.Contains(Music));

    // Rejected calls log one error each and leave the hierarchy untouched.
    AddExpectedError(TEXT("PSL SetSoundClassParent: null child"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("null child"), UPSLAudioBuildLibrary::SetSoundClassParent(nullptr, Master));
    AddExpectedError(TEXT("PSL SetSoundClassParent: class cannot parent itself"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("self parent"), UPSLAudioBuildLibrary::SetSoundClassParent(Master, Master));
    AddExpectedError(TEXT("PSL SetSoundClassParent: cycle"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("cycle"), UPSLAudioBuildLibrary::SetSoundClassParent(Master, Music));
    TestTrue(TEXT("master still root"), Master->ParentClass == nullptr);
    TestFalse(TEXT("music has no children"), Music->ChildClasses.Contains(Master));
    ExpectParent(TEXT("after rejects"), Music, Master);
    TestEqual(TEXT("master children after rejects"), Master->ChildClasses.Num(), 2);

    // nullptr parent detaches to root.
    TestTrue(TEXT("detach sfx"), UPSLAudioBuildLibrary::SetSoundClassParent(Sfx, nullptr));
    TestTrue(TEXT("sfx root"), Sfx->ParentClass == nullptr);
    TestFalse(TEXT("master dropped sfx"), Master->ChildClasses.Contains(Sfx));
    return true;
}
