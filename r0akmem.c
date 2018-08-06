/*++

Copyright (c) Alex Ionescu.  All rights reserved.

Module Name:

    r0akmem.c

Abstract:

    This module implements kernel memory allocation and mapping for r0ak

Author:

    Alex Ionescu (@aionescu) 21-Jul-2018 - First public version

Environment:

    User mode only.

--*/

#include "r0ak.h"

//
// Internal definitions
//
#define POOL_TAG_FIXED_BUFFER       (32 * 1024 * 1024)
#define PAGE_SIZE                   4096
#define NPFS_DATA_ENTRY_SIZE        0x30
#define NPFS_DATA_ENTRY_POOL_TAG    'rFpN'

//
// Tracks allocation state between calls
//
typedef struct _KERNEL_ALLOC
{
    HANDLE Pipes[2];
    PVOID UserBase;
    PVOID KernelBase;
    ULONG MagicSize;
} KERNEL_ALLOC, *PKERNEL_ALLOC;

_Success_(return != 0)
PVOID
GetKernelAddress (
    _In_ ULONG Size
    )
{
    NTSTATUS status;
    PSYSTEM_BIGPOOL_INFORMATION bigPoolInfo;
    PSYSTEM_BIGPOOL_ENTRY entry;
    ULONG resultLength;
    ULONG_PTR resultAddress;
    ULONG i;

    //
    // Allocate a large 32MB buffer to store pool tags in
    //
    bigPoolInfo = VirtualAlloc(NULL,
                               POOL_TAG_FIXED_BUFFER,
                               MEM_COMMIT | MEM_RESERVE,
                               PAGE_READWRITE);
    if (!bigPoolInfo)
    {
        printf("[-] No memory for pool buffer\n");
        return NULL;
    }

    //
    // Dump all pool tags
    //
    status = NtQuerySystemInformation(SystemBigPoolInformation,
                                      bigPoolInfo,
                                      POOL_TAG_FIXED_BUFFER,
                                      &resultLength);
    if (!NT_SUCCESS(status))
    {
        printf("[-] Failed to dump pool allocations: %lx\n", status);
        return NULL;
    }

    //
    // Scroll through them all
    //
    for (resultAddress = 0, i = 0; i < bigPoolInfo->Count; i++)
    {
        //
        // Check for the desired allocation
        //
        entry = &bigPoolInfo->AllocatedInfo[i];
        if (entry->TagUlong == NPFS_DATA_ENTRY_POOL_TAG)
        {
            //
            // With the Heap-Backed Pool in RS5/19H1, sizes are precise, while
            // the large pool allocator uses page-aligned pages
            //
            if ((entry->SizeInBytes == (Size + PAGE_SIZE)) ||
                (entry->SizeInBytes == (Size + NPFS_DATA_ENTRY_SIZE)))
            {
                //
                // Mask out the nonpaged pool bit
                //
                resultAddress = (ULONG_PTR)entry->VirtualAddress & ~1;
                break;
            }
        }
    }

    //
    // Weird..
    //
    if (resultAddress == 0)
    {
        printf("[-] Kernel buffer not found!\n");
        return NULL;
    }

    //
    // Free the buffer
    //
    VirtualFree(bigPoolInfo, 0, MEM_RELEASE);
    return (PVOID)(resultAddress + NPFS_DATA_ENTRY_SIZE);
}

_Success_(return != 0)
PVOID
KernelAlloc (
    _Outptr_ PKERNEL_ALLOC* KernelAlloc,
    _In_ ULONG Size
    )
{
    BOOL b;

    //
    // Catch bad callers
    //
    if (KernelAlloc == NULL)
    {
        return NULL;
    }

    //
    // Only support < 2KB allocations
    //
    *KernelAlloc = NULL;
    if (Size > 2048)
    {
        return NULL;
    }

    //
    // Allocate our tracker structure
    //
    *KernelAlloc = HeapAlloc(GetProcessHeap(),
                             HEAP_ZERO_MEMORY,
                             sizeof(**KernelAlloc));
    if (*KernelAlloc == NULL)
    {
        return NULL;
    }

    //
    // Compute a magic size to get something in big pool that should be unique
    // This will use at most ~5MB of non-paged pool
    //
    (*KernelAlloc)->MagicSize = 0;
    while ((*KernelAlloc)->MagicSize == 0)
    {
        (*KernelAlloc)->MagicSize = (((__rdtsc() & 0xFF000000) >> 24) * 0x5000);
    }

    //
    // Allocate the right child page that will be sent to the trampoline
    //
    (*KernelAlloc)->UserBase = VirtualAlloc(NULL,
                                            (*KernelAlloc)->MagicSize,
                                            MEM_COMMIT | MEM_RESERVE,
                                            PAGE_READWRITE);
    if ((*KernelAlloc)->UserBase == NULL)
    {
        printf("[-] Failed to allocate user-mode memory for kernel buffer\n");
        return NULL;
    }

    //
    // Allocate a pipe to hold on to the buffer
    //
    b = CreatePipe(&(*KernelAlloc)->Pipes[0],
                   &(*KernelAlloc)->Pipes[1],
                   NULL,
                   (*KernelAlloc)->MagicSize);
    if (!b)
    {
        printf("[-] Failed creating the pipe: %lx\n",
               GetLastError());
        return NULL;
    }

    //
    // Return the allocated user-mode base
    //
    return (*KernelAlloc)->UserBase;
}

_Success_(return != 0)
PVOID
KernelWrite (
    _In_ PKERNEL_ALLOC KernelAlloc
    )
{
    BOOL b;

    //
    // Write into the buffer
    //
    b = WriteFile(KernelAlloc->Pipes[1],
                  KernelAlloc->UserBase,
                  KernelAlloc->MagicSize,
                  NULL,
                  NULL);
    if (!b)
    {
        printf("[-] Failed writing kernel buffer: %lx\n",
               GetLastError());
        return NULL;
    }

    //
    // Compute the kernel address and return it
    //
    KernelAlloc->KernelBase = GetKernelAddress(KernelAlloc->MagicSize);
    return KernelAlloc->KernelBase;
}

VOID
KernelFree (
    _In_ PKERNEL_ALLOC KernelAlloc
    )
{
    //
    // Free the UM side of the allocation
    //
    VirtualFree(KernelAlloc->UserBase, 0, MEM_RELEASE);

    //
    // Close the pipes, which will free the kernel side
    //
    CloseHandle(KernelAlloc->Pipes[0]);
    CloseHandle(KernelAlloc->Pipes[1]);

    //
    // Free the structure
    //
    HeapFree(GetProcessHeap(), 0, KernelAlloc);
}

