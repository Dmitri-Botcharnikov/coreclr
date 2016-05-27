#include <pal.h>
#include <new.hpp>

#include "classfactory.h"
#include "profilercallback.h"

//
// Helpers/Registration
//
HINSTANCE g_hInst; // Instance handle to this piece of code

// TODO: Taken from Demo Profiler. Replace with unique ID
EXTERN_GUID(CLSID_PROFILER, 0x912E73AF, 0xF51D, 0x4E80, 0x89, 0x4D, 0xF4, 0xE9, 0xE6, 0xDD, 0x7C, 0x2E);
#define PROFILER_GUID W("{912E73AF-F51D-4E80-894D-F4E9E6DD7C2E}")

// This map contains the list of coclasses which are exported from this module
const COCLASS_REGISTER g_CoClasses[] =
{
//   pClsid           szProgID       pfnCreateObject
    {&CLSID_PROFILER, PROFILER_GUID, ProfilerCallback::CreateObject},
    {NULL,            NULL,          NULL                          }
};

// HINSTANCE hInstance : Instance handle
// DWORD dwReason : one of DLL_PROCESS_ATTACH, DLL_PROCESS_DETACH,
//     DLL_THREAD_ATTACH, DLL_THREAD_DETACH
//
// LPVOID lpReserved :
//     If dwReason is DLL_PROCESS_ATTACH, lpvReserved is NULL for dynamic loads and non-NULL for static loads.
//     If dwReason is DLL_PROCESS_DETACH, lpvReserved is NULL if DllMain has been called by using FreeLibrary
//         and non-NULL if DllMain has been called during process termination.
//
// (no return value)
extern "C"
BOOL WINAPI DllMain(
    HINSTANCE hInstance,
    DWORD dwReason,
    LPVOID lpReserved)
{
    // Save off the instance handle for later use
    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
            g_hInst = hInstance;
            DisableThreadLibraryCalls(hInstance);
            break;
        case DLL_PROCESS_DETACH:
            // lpReserved == NULL means that we called FreeLibrary()
            // in that case do nothing
            if ((lpReserved != NULL) && (g_pCallbackObject != NULL))
                g_pCallbackObject->DllDetachShutdown();
            break;
        default:
            break;
    }

    return TRUE;
}

STDAPI DllGetClassObject( // Return code
    REFCLSID    rclsid,   // The class to desired
    REFIID      riid,     // Interface wanted on class factory
    LPVOID FAR *ppv)      // Return interface pointer here
{
    ProfClassFactory *pClassFactory;  // To create class factory object
    const COCLASS_REGISTER *pCoClass; // Loop control
    HRESULT hr = CLASS_E_CLASSNOTAVAILABLE;

    // Scan for the right one
    for (pCoClass = g_CoClasses; pCoClass->pClsid != NULL; pCoClass++)
    {
        if (*pCoClass->pClsid == rclsid)
        {
            // Allocate the new factory object
            pClassFactory = new (nothrow) ProfClassFactory(pCoClass);
            if (!pClassFactory)
                return E_OUTOFMEMORY;

            // Pick the v-table based on the caller's request
            hr = pClassFactory->QueryInterface(riid, ppv);

            // Always release the local reference, if QI failed it will be
            // the only one and the object gets freed
            pClassFactory->Release();
            break;
        }
    }

    return hr;
}

HINSTANCE GetModuleInst()
{
    return g_hInst;
}
