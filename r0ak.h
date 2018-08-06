/*++

Copyright (c) Alex Ionescu.  All rights reserved.

Module Name:

    r0ak.h

Abstract:

    This header defines the main routines and structures for r0ak 

Author:

    Alex Ionescu (@aionescu) 21-Jul-2018 - First public version

Environment:

    User mode only.

--*/

#define UNICODE
#include <initguid.h>
#include <windows.h>
#include <tlhelp32.h>
#include <Psapi.h>
#include <DbgHelp.h>
#include <stdlib.h>
#include <stdio.h>
#include <winternl.h>
#include <evntcons.h>
#include <Evntrace.h>
#include "nt.h"

//
// Symbols provided by the symbol engine
//
extern PVOID g_XmFunction;
extern PVOID g_HstiBufferSize;
extern PVOID g_HstiBufferPointer;
extern PVOID g_TrampolineFunction;

//
// Opaque to callers
//
typedef struct _KERNEL_ALLOC *PKERNEL_ALLOC;
typedef struct _KERNEL_EXECUTE *PKERNEL_EXECUTE;
typedef struct _ETW_DATA *PETW_DATA;

//
// Symbol Routines
//
_Success_(return != 0)
PVOID
SymLookup (
    _In_ PCHAR ModuleName,
    _In_ PCHAR SymbolName
    );

_Success_(return != 0)
BOOL
SymSetup (
    VOID
    );

//
// Utility Routines
//
VOID
DumpHex (
    _In_ LPCVOID Data,
    _In_ SIZE_T Size
    );

_Success_(return != 0)
ULONG_PTR
GetDriverBaseAddr (
    _In_ PCCH BaseName
    );

_Success_(return != 0)
BOOL
ElevateToSystem (
    VOID
    );

//
// Kernel Memory Routines
//
_Success_(return != 0)
PVOID
KernelAlloc (
    _Outptr_ PKERNEL_ALLOC* KernelAlloc,
    _In_ ULONG Size
    );

_Success_(return != 0)
PVOID
KernelWrite (
    _In_ PKERNEL_ALLOC KernelAlloc
    );

VOID
KernelFree (
    _In_ PKERNEL_ALLOC KernelAlloc
    );

//
// Kernel Execution Routines
//
_Success_(return != 0)
BOOL
KernelExecuteRun (
    _In_ PKERNEL_EXECUTE KernelExecute
    );

_Success_(return != 0)
BOOL
KernelExecuteSetup (
    _Outptr_ PKERNEL_EXECUTE* KernelExecute,
    _In_ PVOID TrampolineFunction
    );

_Success_(return != 0)
BOOL
KernelExecuteSetCallback (
    _In_ PKERNEL_EXECUTE KernelExecute,
    _In_ PVOID WorkFunction,
    _In_ PVOID WorkParameter
    );

VOID
KernelExecuteTeardown (
    _In_ PKERNEL_EXECUTE KernelExecute
    );

//
// Kernel Read Routine
//
_Success_(return != 0)
BOOL
CmdReadKernel (
    _In_ PKERNEL_EXECUTE KernelExecute,
    _In_ PVOID KernelAddress,
    _In_ ULONG ValueSize
    );

//
// Kernel Write Routine
//
_Success_(return != 0)
BOOL
CmdWriteKernel (
    _In_ PKERNEL_EXECUTE KernelExecute,
    _In_ PVOID KernelAddress,
    _In_ ULONG KernelValue
    );

//
// Kernel Run Routine
//
_Success_(return != 0)
BOOL
CmdExecuteKernel (
    _In_ PKERNEL_EXECUTE KernelExecute,
    _In_ PVOID FunctionPointer,
    _In_ ULONG_PTR FunctionParameter
    );

//
// ETW Routines
//
_Success_(return != 0)
BOOL
EtwStartSession (
    _Outptr_ PETW_DATA* EtwData,
    _In_ PVOID WorkerRoutine
    );

_Success_(return != 0)
BOOL
EtwParseSession (
    _In_ PETW_DATA EtwData
    );

