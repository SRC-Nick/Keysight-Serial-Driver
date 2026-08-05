#include <windows.h>

#include "src/SRCSerial_Internal.h"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        srcserial::Initialize(module);
        srcserial::InitializeWorker();
    }
    else if (reason == DLL_PROCESS_DETACH && reserved == NULL)
    {
        srcserial::ShutdownWorker(false);
        srcserial::Shutdown();
    }
    return TRUE;
}
