// Program entry: set up the GC and reflection, then run TowerFall's Main.
#include "tf_all.h"
#include "platform/platform.h"
#include <cstdio>
#include <SDL.h>

void __tf_register_roots();
void __tf_register_reflection();

namespace plat { const char* autotest_config(const char* key); }
namespace Microsoft { namespace Xna { namespace Framework { void xna_shutdown(); } } }
static int RealMain(int argc, char** argv)
{
    if (plat::autotest_config("TF_CENSUS")) ::sys::gc_enable_census(true);
    if (plat::autotest_config("TF_VPADS")) { ::TowerFall::MainMenu::__cinit(); ::TowerFall::MainMenu::NoQuit = true; }
    __tf_register_roots();
    __tf_register_reflection();
    auto* args = ::System::Array<::System::String*>::New(0);
    try
    {
        ::TowerFall::TFGame::Main(args);
    }
    catch (::System::Exception* e)
    {
        std::string m = "Unhandled exception: " + e->ToString()->ToUtf8();
        plat::log(m.c_str());
        ::Microsoft::Xna::Framework::xna_shutdown();
        return 1;
    }
    // TFGame.Main catches exceptions thrown out of Game.Run, which then skipped the shutdown.
    // Without it the Wii U never finishes the ProcUI exit and can't return to the menu.
    ::Microsoft::Xna::Framework::xna_shutdown();
    return 0;
}

#if defined(__WIIU__)
#include <coreinit/thread.h>
int main(int argc, char** argv)
{
    // Runs on the real main thread (ProcUI/HOME handling); the RPX's main-thread stack is
    // enlarged to 8 MB at build time by wiiu/patch_rpx_stack.py.
    volatile int stackTop = 0;
    ::sys::gc_init((void*)&stackTop);
    OSThread* self = OSGetCurrentThread();
    char b[160];
    snprintf(b, sizeof b, "main thread stack: %u KB", (unsigned)(((uintptr_t)self->stackStart - (uintptr_t)self->stackEnd) / 1024));
    plat::log(b);
    return RealMain(argc, argv);
}
#else
int main(int argc, char** argv)
{
    volatile int stackTop = 0;
    ::sys::gc_init((void*)&stackTop);
    return RealMain(argc, argv);
}
#endif
