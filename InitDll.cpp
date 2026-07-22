#include <windows.h>

#include "src/SRCSerial_Internal.h"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        srcserial::Initialize(module);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        srcserial::Shutdown();
    }
    return TRUE;
}
