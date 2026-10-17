// Implementations of TowerFall.Native (port/cs/Port/Native.cs)
#include "tf_all.h"
#include "../platform/platform.h"

::System::String* TowerFall::Native::GetSavePath()
{
    std::string p = plat::save_dir();
    ::System::IO::Directory::CreateDirectory(::System::String::FromUtf8(p.c_str()));
    return ::System::String::FromUtf8(p.c_str());
}

uint32_t TowerFall::Native::GamePadGameButtons(int32_t index)
{
    return plat::pad_game_buttons(index);
}

::System::String* TowerFall::Native::GetPlatform()
{
    if (plat::is_wiiu()) return TF_S(u"Wii U");
    if (const char* p = getenv("TF_PLATFORM")) return ::System::String::FromUtf8(p);   // testing
    return TF_S(u"Desktop");
}
