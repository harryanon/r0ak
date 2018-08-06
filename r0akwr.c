/*++

Copyright (c) Alex Ionescu.  All rights reserved.

Module Name:

    r0akwr.c

Abstract:

    This module implements write capabilities for r0ak

Author:

    Alex Ionescu (@aionescu) 21-Jul-2018 - First public version

Environment:

    User mode only.

--*/

#include "r0ak.h"

typedef enum _XM_OPERATION_DATATYPE
{
    BYTE_DATA = 0,
    WORD_DATA = 1,
    LONG_DATA = 3
} XM_OPERATION_DATATYPE;

typedef struct _XM_CONTEXT
{
    UCHAR Reserved[0x58];
    PVOID DestinationPointer;
    PVOID SourcePointer;
    ULONG DestinationValue;
    ULONG SourceValue;
    ULONG CurrentOpcode;
    ULONG DataSegment;
    ULONG DataType;
} XM_CONTEXT, *PXM_CONTEXT;

_Success_(return != 0)
BOOL
CmdWriteKernel (
    _In_ PKERNEL_EXECUTE KernelExecute,
    _In_ PVOID KernelAddress,
    _In_ ULONG KernelValue
    )
{
    PKERNEL_ALLOC kernelAlloc;
    PXM_CONTEXT xmContext;
    BOOL b;
    PETW_DATA etwData;

    //
    // Trace operation
    //
    printf("[+] Writing 0x%.8lX to                                0x%.16p\n",
           KernelValue, KernelAddress);

    //
    // Allocate an XM_CONTEXT to drive the HAL x64 emulator
    //
    kernelAlloc = NULL;
    xmContext = KernelAlloc(&kernelAlloc, sizeof(*xmContext));
    if (xmContext == NULL)
    {
        printf("[-] Failed to allocate memory for XM_CONTEXT\n");
        return FALSE;
    }

    //
    // Fill it out
    //
    xmContext->SourceValue = KernelValue;
    xmContext->DataType = LONG_DATA;
    xmContext->DestinationPointer = KernelAddress;

    //
    // Make a kernel copy of it
    //
    xmContext = KernelWrite(kernelAlloc);
    if (xmContext == NULL)
    {
        printf("[-] Failed to find kernel memory for XM_CONTEXT\n");
        KernelFree(kernelAlloc);
        return FALSE;
    }

    //
    // Setup the work item
    //
    b = KernelExecuteSetCallback(KernelExecute,
                                 g_XmFunction,
                                 xmContext->Reserved);
    if (b == FALSE)
    {
        printf("[-] Failed to initialize work item!\n");
        KernelFree(kernelAlloc);
        return b;
    }

    //
    // Begin ETW tracing to look for the work item executing
    //
    etwData = NULL;
    b = EtwStartSession(&etwData, g_XmFunction);
    if (b == FALSE)
    {
        printf("[-] Failed to start ETW trace\n");
        KernelFree(kernelAlloc);
        return b;
    }

    //
    // Run it!
    //
    b = KernelExecuteRun(KernelExecute);
    if (b == FALSE)
    {
        printf("[-] Failed to execute kernel function!\n");
    }
    else
    {
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
            return b;
        }
    }

    //
    // Free the allocation since this path either failed or completed execution
    //
    KernelFree(kernelAlloc);
    return b;
}
