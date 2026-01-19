#include "Private.h"
#include "Globals.h"
#include "Logger.h"

//+---------------------------------------------------------------------------
//
// DllMain
//
//----------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID pvReserved)
{
    pvReserved;

    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
        Global::StartLog();
        Global::Log(L"DLL_PROCESS_ATTACH");

        Global::dllInstanceHandle = hInstance;

        if (!InitializeCriticalSectionAndSpinCount(&Global::CS, 0))
        {
             Global::Log(L"InitializeCriticalSectionAndSpinCount failed");
            return FALSE;
        }

        if (!Global::RegisterWindowClass()) {
             Global::Log(L"RegisterWindowClass failed");
            return FALSE;
        }

        break;

    case DLL_PROCESS_DETACH:
        Global::Log(L"DLL_PROCESS_DETACH");

        DeleteCriticalSection(&Global::CS);

        break;

    case DLL_THREAD_ATTACH:
        // Global::Log(L"DLL_THREAD_ATTACH");
        break;

    case DLL_THREAD_DETACH:
        // Global::Log(L"DLL_THREAD_DETACH");
        break;
    }

    return TRUE;
}
