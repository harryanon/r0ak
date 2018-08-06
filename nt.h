/*++

Copyright (c) Alex Ionescu.  All rights reserved.

Module Name:

    nt.h

Abstract:

    This header defines internal NT data structures and routines used by r0ak

Author:

    Alex Ionescu (@aionescu) 21-Jul-2018 - First public version

Environment:

    User mode only.

--*/

#include <winternl.h>

NTSYSAPI
NTSTATUS
NTAPI
RtlAdjustPrivilege (
    _In_ ULONG Privilege,
    _In_ BOOLEAN NewValue,
    _In_ BOOLEAN ForThread,
    _Out_ PBOOLEAN OldValue
    );

NTSYSAPI
NTSTATUS
NTAPI
ZwOpenSection (
    _Out_ PHANDLE SectionHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes
    );

typedef struct _WORK_QUEUE_ITEM
{
    LIST_ENTRY List;
    PVOID WorkerRoutine;
    PVOID Parameter;
} WORK_QUEUE_ITEM, *PWORK_QUEUE_ITEM;

typedef struct _RTL_BALANCED_LINKS
{
    struct _RTL_BALANCED_LINKS *Parent;
    struct _RTL_BALANCED_LINKS *LeftChild;
    struct _RTL_BALANCED_LINKS *RightChild;
    CHAR Balance;
    UCHAR Reserved[3];
} RTL_BALANCED_LINKS;
typedef RTL_BALANCED_LINKS *PRTL_BALANCED_LINKS;

typedef struct _RTL_AVL_TABLE
{
    RTL_BALANCED_LINKS BalancedRoot;
    PVOID OrderedPointer;
    ULONG WhichOrderedElement;
    ULONG NumberGenericTableElements;
    ULONG DepthOfTree;
    PRTL_BALANCED_LINKS RestartKey;
    ULONG DeleteCount;
    PVOID CompareRoutine;
    PVOID AllocateRoutine;
    PVOID FreeRoutine;
    PVOID TableContext;
} RTL_AVL_TABLE;
typedef RTL_AVL_TABLE *PRTL_AVL_TABLE;

typedef struct tagXSGLOBALS
{
    PVOID NetworkFontsTableLock;
    PRTL_AVL_TABLE NetworkFontsTable;
    PVOID TrustedFontsTableLock;
    PRTL_AVL_TABLE TrustedFontsTable;
} XSGLOBALS, *PXSGLOBALS;

#pragma warning(push)
#pragma warning(disable:4214)
#pragma warning(disable:4201)
typedef struct _SYSTEM_BIGPOOL_ENTRY
{
    union
    {
        PVOID VirtualAddress;
        ULONG_PTR NonPaged : 1;
    };
    ULONGLONG SizeInBytes;
    union
    {
        UCHAR Tag[4];
        ULONG TagUlong;
    };
} SYSTEM_BIGPOOL_ENTRY, *PSYSTEM_BIGPOOL_ENTRY;
#pragma warning(pop)

typedef struct _SYSTEM_BIGPOOL_INFORMATION
{
    ULONG Count;
    SYSTEM_BIGPOOL_ENTRY AllocatedInfo[ANYSIZE_ARRAY];
} SYSTEM_BIGPOOL_INFORMATION, *PSYSTEM_BIGPOOL_INFORMATION;

#define SE_DEBUG_PRIVILEGE 20
#define SystemBigPoolInformation (SYSTEM_INFORMATION_CLASS)66
#define SystemHardwareSecurityTestInterfaceResultsInformation (SYSTEM_INFORMATION_CLASS)166
#define PERF_WORKER_THREAD 0x48000000
#define EVENT_TRACE_GROUP_THREAD 0x0500
#define PERFINFO_LOG_TYPE_WORKER_THREAD_ITEM_END (EVENT_TRACE_GROUP_THREAD | 0x41)

