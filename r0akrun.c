/*++

Copyright (c) Alex Ionescu.  All rights reserved.

Module Name:

    r0akrun.c

Abstract:

    This module implements run capabilities for r0ak

Author:

    Alex Ionescu (@aionescu) 21-Jul-2018 - First public version

Environment:

    User mode only.

--*/

#include "r0ak.h"

_Success_(return != 0)
BOOL
CmdExecuteKernel (
    _In_ PKERNEL_EXECUTE KernelExecute,
    _In_ PVOID FunctionPointer,
    _In_ ULONG_PTR FunctionParameter
    )
{
    PETW_DATA etwData;
    BOOL b;

    //
    // Initialize a work item for the caller-supplied function and argument
    //
    printf("[+] Calling function pointer 0x%p\n", FunctionPointer);
    b = KernelExecuteSetCallback(KernelExecute,
                                 FunctionPointer,
                                 (PVOID)FunctionParameter);
    if (b == FALSE)
    {
        printf("[-] Failed to initialize work item trampoline\n");
        return b;
    }

    //
    // Begin ETW tracing to look for the work item executing
    //
    etwData = NULL;
    b = EtwStartSession(&etwData, FunctionPointer);
    if (b == FALSE)
    {
        printf("[-] Failed to start ETW trace\n");
        return b;
    }

    //
    // Execute it!
    //
    b = KernelExecuteRun(KernelExecute);
    if (b == FALSE)
    {
        printf("[-] Failed to execute work item\n");
        return b;
    }

    //
    // Wait for execution to finish
    //
    b = EtwParseSession(etwData);
    if (b == FALSE)
    {
        //
        // We have no idea if execution finished -- block forever
        //
        printf("[-] Failed to parse ETW trace\n");
        Sleep(INFINITE);
    }
    return b;
}
