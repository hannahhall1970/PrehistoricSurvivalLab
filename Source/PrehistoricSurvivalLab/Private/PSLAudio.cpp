#include "PSLAudio.h"

#include "Sound/SoundBase.h"

USoundBase* PSLAudio::Load(const TCHAR* Path)
{
    return LoadObject<USoundBase>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}
