#include "PSLAudioBuildLibrary.h"

#include "Sound/SoundClass.h"

bool UPSLAudioBuildLibrary::SetSoundClassParent(USoundClass* Child, USoundClass* Parent)
{
    if (!Child)
    {
        UE_LOG(LogTemp, Error, TEXT("PSL SetSoundClassParent: null child"));
        return false;
    }
    if (Parent == Child)
    {
        UE_LOG(LogTemp, Error, TEXT("PSL SetSoundClassParent: class cannot parent itself"));
        return false;
    }
    for (const USoundClass* Ancestor = Parent; Ancestor; Ancestor = Ancestor->ParentClass)
    {
        if (Ancestor == Child)
        {
            UE_LOG(LogTemp, Error, TEXT("PSL SetSoundClassParent: cycle"));
            return false;
        }
    }

    USoundClass* OldParent = Child->ParentClass;
    if (OldParent && OldParent != Parent)
    {
        OldParent->Modify();
        OldParent->ChildClasses.Remove(Child);
        OldParent->MarkPackageDirty();
    }
    Child->Modify();
    Child->ParentClass = Parent;
    Child->MarkPackageDirty();
    if (Parent)
    {
        Parent->Modify();
        Parent->ChildClasses.AddUnique(Child);
        Parent->MarkPackageDirty();
    }
    return true;
}
