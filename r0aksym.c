/*++

Copyright (c) Alex Ionescu.  All rights reserved.

Module Name:

    r0aksym.c

Abstract:

    This module implements the symbol engine interface and parsing for r0ak

Author:

    Alex Ionescu (@aionescu) 21-Jul-2018 - First public version

Environment:

    User mode only.

--*/

#include "r0ak.h"

typedef DWORD64
(*tSymLoadModuleEx)(
    _In_ HANDLE hProcess,
    _In_opt_ HANDLE hFile,
    _In_opt_ PCSTR ImageName,
    _In_opt_ PCSTR ModuleName,
    _In_ DWORD64 BaseOfDll,
    _In_ DWORD DllSize,
    _In_opt_ PMODLOAD_DATA Data,
    _In_opt_ DWORD Flags
    );

typedef BOOL
(*tSymInitializeW)(
    _In_ HANDLE hProcess,
    _In_opt_ PCWSTR UserSearchPath,
    _In_ BOOL fInvadeProcess
    );

typedef DWORD
(*tSymSetOptions)(
    _In_ DWORD   SymOptions
    );

typedef BOOL
(*tSymGetSymFromName64)(
    _In_ HANDLE hProcess,
    _In_ PCSTR Name,
    _Inout_ PIMAGEHLP_SYMBOL64 Symbol
    );

typedef BOOL
(*tSymUnloadModule64)(
    _In_ HANDLE hProcess,
    _In_ DWORD64 BaseOfDll
    );

PVOID g_XmFunction;
PVOID g_HstiBufferSize;
PVOID g_HstiBufferPointer;
PVOID g_TrampolineFunction;

tSymLoadModuleEx pSymLoadModuleEx;
tSymInitializeW pSymInitializeW;
tSymSetOptions pSymSetOptions;
tSymUnloadModule64 pSymUnloadModule64;
tSymGetSymFromName64 pSymGetSymFromName64;

_Success_(return != 0)
PVOID
SymLookup (
    _In_ PCHAR ModuleName,
    _In_ PCHAR SymbolName
    )
{
    ULONG_PTR offset;
    ULONG_PTR kernelBase;
    ULONG_PTR imageBase;
    BOOL b;
    PIMAGEHLP_SYMBOL64 symbol;
    ULONG_PTR realKernelBase;
    CHAR symName[MAX_PATH];

    //
    // Get the base address of the kernel image in kernel-mode
    //
    realKernelBase = GetDriverBaseAddr(ModuleName);
    if (realKernelBase == 0)
    {
        printf("[-] Couldn't find base address for %s\n", ModuleName);
        return NULL;
    }

    //
    // Load the kernel image in user-mode
    //
    kernelBase = (ULONG_PTR)LoadLibraryExA(ModuleName,
                                           NULL,
                                           DONT_RESOLVE_DLL_REFERENCES);
    if (kernelBase == 0)
    { 
        printf("[-] Couldn't map %s!\n", ModuleName);
        return NULL;
    }

    //
    // Allocate space for a symbol buffer
    //
    symbol = HeapAlloc(GetProcessHeap(),
                       HEAP_ZERO_MEMORY,
                       sizeof(*symbol) + 2);
    if (symbol == NULL)
    {
        printf("[-] Not enough memory to allocate IMAGEHLP_SYMBOL64\n");
        FreeLibrary((HMODULE)kernelBase);
        return NULL;
    }

    //
    // Attach symbols to our module
    //
    imageBase = pSymLoadModuleEx(GetCurrentProcess(),
                                 NULL,
                                 ModuleName,
                                 ModuleName,
                                 kernelBase,
                                 0,
                                 NULL,
                                 0);
    if (imageBase != kernelBase)
    {
        HeapFree(GetProcessHeap(), 0, symbol);
        FreeLibrary((HMODULE)kernelBase);
        printf("[-] Couldn't load symbols for %s\n", ModuleName);
        return NULL;
    }

    //
    // Build the symbol name
    //
    strcpy_s(symName, MAX_PATH, ModuleName);
    strcat_s(symName, MAX_PATH, "!");
    strcat_s(symName, MAX_PATH, SymbolName);

    //
    // Look it up
    //
    symbol->SizeOfStruct = sizeof(*symbol);
    symbol->MaxNameLength = 1;
    b = pSymGetSymFromName64(GetCurrentProcess(), symName, symbol);
    if (b == FALSE)
    {
        printf("[-] Couldn't find %s symbol\n", symName);
        FreeLibrary((HMODULE)kernelBase);
        pSymUnloadModule64(GetCurrentProcess(), imageBase);
        HeapFree(GetProcessHeap(), 0, symbol);
        return NULL;
    }
    
    //
    // Compute the offset based on the mapped address
    //
    offset = symbol->Address - kernelBase;
    FreeLibrary((HMODULE)kernelBase);
    pSymUnloadModule64(GetCurrentProcess(), imageBase);
    HeapFree(GetProcessHeap(), 0, symbol);

    //
    // Compute the final location based on the real kernel base
    //
    return (PVOID)(realKernelBase + offset);
}

_Success_(return != 0)
BOOL
SymSetup (
    VOID
    )
{
    HMODULE hMod;
    HKEY rootKey;
    DWORD dwError;
    WCHAR rootPath[MAX_PATH];
    ULONG pathSize;
    ULONG type;
    BOOL b;

    //
    // Open the Kits key
    //
    dwError = RegOpenKey(HKEY_LOCAL_MACHINE,
                         L"Software\\Microsoft\\Windows Kits\\Installed Roots",
                         &rootKey);
    if (dwError != ERROR_SUCCESS)
    {
        printf("[-] No Windows SDK or WDK installed: %lx\n", dwError);
        return FALSE;
    }

    //
    // Check where a kit was installed
    //
    pathSize = sizeof(rootPath);
    type = REG_SZ;
    dwError = RegQueryValueEx(rootKey,
                              L"KitsRoot10",
                              NULL,
                              &type,
                              (LPBYTE)rootPath,
                              &pathSize);
    if (dwError != ERROR_SUCCESS)
    {
        printf("[-] Win 10 SDK/WDK not found, falling back to 8.1: %lx\n",
               dwError);
        dwError = RegQueryValueEx(rootKey,
                                  L"KitsRoot81",
                                  NULL,
                                  &type,
                                  (LPBYTE)rootPath,
                                  &pathSize);
        if (dwError != ERROR_SUCCESS)
        {
            printf("[-] Win 8.1 SDK/WDK not found, falling back to 8: %lx\n",
                   dwError);
            dwError = RegQueryValueEx(rootKey,
                                      L"KitsRoot8",
                                      NULL,
                                      &type,
                                      (LPBYTE)rootPath,
                                      &pathSize);
            if (dwError != ERROR_SUCCESS)
            {
                printf("[-] Win 8 SDK/WDK not found %lx\n", dwError);
                return FALSE;
            }
        }
    }

    //
    // Now try to load the correct debug help library
    //
    wcscat_s(rootPath, _ARRAYSIZE(rootPath), L"debuggers\\x64\\dbghelp.dll");
    hMod = LoadLibrary(rootPath);
    if (hMod == NULL)
    {
        printf("[-] Failed to load Debugging Tools Dbghelp.dll: %lx\n",
               GetLastError());
        return FALSE;
    }

    //
    // Get the APIs that we need
    //
    pSymSetOptions = (tSymSetOptions)GetProcAddress(hMod,
                                                    "SymSetOptions");
    if (pSymSetOptions == NULL)
    {
        printf("[-] Failed to find SymSetOptions\n");
        return FALSE;
    }
    pSymInitializeW = (tSymInitializeW)GetProcAddress(hMod,
                                                      "SymInitializeW");
    if (pSymInitializeW == NULL)
    {
        printf("[-] Failed to find SymInitializeW\n");
        return FALSE;
    }
    pSymLoadModuleEx = (tSymLoadModuleEx)GetProcAddress(hMod,
                                                        "SymLoadModuleEx");
    if (pSymLoadModuleEx == NULL)
    {
        printf("[-] Failed to find SymLoadModuleEx\n");
        return FALSE;
    }
    pSymGetSymFromName64 = (tSymGetSymFromName64)GetProcAddress(hMod,
                                                                "SymGetSymFromName64");
    if (pSymGetSymFromName64 == NULL)
    {
        printf("[-] Failed to find SymGetSymFromName64\n");
        return FALSE;
    }
    pSymUnloadModule64 = (tSymUnloadModule64)GetProcAddress(hMod,
                                                            "SymUnloadModule64");
    if (pSymUnloadModule64 == NULL)
    {
        printf("[-] Failed to find SymUnloadModule64\n");
        return FALSE;
    }

    //
    // Initialize the engine
    //
    pSymSetOptions(SYMOPT_DEFERRED_LOADS);
    b = pSymInitializeW(GetCurrentProcess(), NULL, TRUE);
    if (b == FALSE)
    {
        printf("[-] Failed to initialize symbol engine: %lx\n",
               GetLastError());
        return b;
    }

    //
    // Initialize our gadgets
    //
    g_XmFunction = SymLookup("hal.dll", "XmMovOp");
    if (g_XmFunction == NULL)
    {
        printf("[-] Failed to find hal!XmMovOp\n");
        return FALSE;
    }
    g_HstiBufferSize = SymLookup("ntoskrnl.exe", "SepHSTIResultsSize");
    if (g_HstiBufferSize == NULL)
    {
        printf("[-] Failed to find nt!SepHSTIResultsSize\n");
        return FALSE;
    }
    g_HstiBufferPointer = SymLookup("ntoskrnl.exe", "SepHSTIResultsBuffer");
    if (g_HstiBufferPointer == NULL)
    {
        printf("[-] Failed to find nt!SepHSTIResultsBuffer\n");
        return FALSE;
    }
    g_TrampolineFunction = SymLookup("ntoskrnl.exe", "PopFanIrpComplete");
    if (g_TrampolineFunction == NULL)
    {
        printf("[-] Failed to find nt!PopFanIrpComplete\n");
        return FALSE;
    }
    return TRUE;
}
