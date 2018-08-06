/*++

Copyright (c) Alex Ionescu.  All rights reserved.

Module Name:

    r0aketw.c

Abstract:

    This module implements the ETW tracing support routines for r0ak

Author:

    Alex Ionescu (@aionescu) 21-Jul-2018 - First public version

Environment:

    User mode only.

--*/
#include "r0ak.h"

//
// Tracks tracing data between calls
//
typedef struct _ETW_DATA
{
    TRACEHANDLE SessionHandle;
    TRACEHANDLE ParserHandle;
    PEVENT_TRACE_PROPERTIES Properties;
    PVOID WorkItemRoutine;
} ETW_DATA, *PETW_DATA;

DEFINE_GUID(g_EtwTraceGuid,
            0x53636210,
            0xbe24,
            0x1264,
            0xc6, 0xa5, 0xf0, 0x9c, 0x59, 0x88, 0x1e, 0xbd);
WCHAR g_EtwTraceName[] = L"r0ak-etw";

VOID
EtpEtwEventCallback(
    _In_ PEVENT_RECORD EventRecord
    )
{
    PETW_DATA etwData;

    //
    // Look for an "end of work item execution event"
    //
    if (EventRecord->EventHeader.EventDescriptor.Opcode ==
        (PERFINFO_LOG_TYPE_WORKER_THREAD_ITEM_END & 0xFF))
    {
        //
        // Grab our context and check if the work routine matches ours
        //
        etwData = (PETW_DATA)EventRecord->UserContext;
        if (*(PVOID*)EventRecord->UserData == etwData->WorkItemRoutine)
        {
            //
            // Stop the trace -- this callback will run a few more times
            //
            printf("[+] Kernel finished executing work item at               0x%.16p\n",
                   etwData->WorkItemRoutine);
            ControlTrace(etwData->SessionHandle,
                         NULL,
                         etwData->Properties,
                         EVENT_TRACE_CONTROL_STOP);
        }
    }
}

_Success_(return != 0)
BOOL
EtwParseSession (
    _In_ PETW_DATA EtwData
    )
{
    ULONG errorCode;

    //
    // Process the trace until the right work item is found
    //
    errorCode = ProcessTrace(&EtwData->ParserHandle, 1, NULL, NULL);
    if (errorCode != ERROR_SUCCESS)
    {
        printf("[-] Failed to process trace: %lX\n", errorCode);
        ControlTrace(EtwData->SessionHandle,
                     NULL,
                     EtwData->Properties,
                     EVENT_TRACE_CONTROL_STOP);
    }

    //
    // All done -- cleanup
    //
    CloseTrace(EtwData->ParserHandle);
    HeapFree(GetProcessHeap(), 0, EtwData->Properties);
    HeapFree(GetProcessHeap(), 0, EtwData);
    return errorCode == ERROR_SUCCESS;
}

_Success_(return != 0)
BOOL
EtwStartSession (
    _Outptr_ PETW_DATA* EtwData,
    _In_ PVOID WorkItemRoutine
    )
{
    ULONG errorCode;
    ULONG traceFlags[8] = { 0 };
    EVENT_TRACE_LOGFILEW logFile = { 0 };
    ULONG bufferSize;

    //
    // Initialize context
    //
    *EtwData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(**EtwData));
    if (*EtwData == NULL)
    {
        printf("[-] Out of memory allocating ETW state\n");
        return FALSE;
    }

    //
    // Allocate memory for our session descriptor
    //
    bufferSize = sizeof(EVENT_TRACE_PROPERTIES) + sizeof(g_EtwTraceName);
    (*EtwData)->Properties = HeapAlloc(GetProcessHeap(),
                                       HEAP_ZERO_MEMORY,
                                       bufferSize);
    if ((*EtwData)->Properties == NULL)
    {
        printf("[-] Failed to allocate memory for the ETW trace\n");
        HeapFree(GetProcessHeap(), 0, *EtwData);
        return FALSE;
    }

    //
    // Create a real-time session using the system logger, tracing nothing
    //
    (*EtwData)->Properties->Wnode.BufferSize = bufferSize;
    (*EtwData)->Properties->Wnode.Guid = g_EtwTraceGuid;
    (*EtwData)->Properties->Wnode.ClientContext = 1;
    (*EtwData)->Properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    (*EtwData)->Properties->MinimumBuffers = 1;
    (*EtwData)->Properties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE |
                                          EVENT_TRACE_SYSTEM_LOGGER_MODE;
    (*EtwData)->Properties->FlushTimer = 1;
    (*EtwData)->Properties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    errorCode = StartTrace(&(*EtwData)->SessionHandle,
                           g_EtwTraceName,
                           (*EtwData)->Properties);
    if (errorCode != ERROR_SUCCESS)
    {
        printf("[-] Failed to create the event trace session: %lX\n", 
               errorCode);
        HeapFree(GetProcessHeap(), 0, (*EtwData)->Properties);
        HeapFree(GetProcessHeap(), 0, *EtwData);
        return FALSE;
    }

    //
    // Open a consumer handle to it
    //
    logFile.LoggerName = g_EtwTraceName;
    logFile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME |
                               PROCESS_TRACE_MODE_EVENT_RECORD;
    logFile.EventRecordCallback = EtpEtwEventCallback;
    logFile.Context = *EtwData;
    (*EtwData)->ParserHandle = OpenTrace(&logFile);
    if ((*EtwData)->ParserHandle == INVALID_PROCESSTRACE_HANDLE)
    {
        printf("[-] Failed open a consumer handle for the trace session: %lX\n",
               GetLastError());
        ControlTrace((*EtwData)->SessionHandle,
                     NULL,
                     (*EtwData)->Properties,
                     EVENT_TRACE_CONTROL_STOP);
        HeapFree(GetProcessHeap(), 0, (*EtwData)->Properties);
        HeapFree(GetProcessHeap(), 0, *EtwData);
        return FALSE;
    }

    //
    // Trace worker thread events
    //
    traceFlags[2] = PERF_WORKER_THREAD;
    errorCode = TraceSetInformation((*EtwData)->SessionHandle,
                                    TraceSystemTraceEnableFlagsInfo,
                                    traceFlags,
                                    sizeof(traceFlags));
    if (errorCode != ERROR_SUCCESS)
    {
        printf("[-] Failed to set flags for event trace session: %lX\n",
               errorCode);
        ControlTrace((*EtwData)->SessionHandle,
                     NULL,
                     (*EtwData)->Properties,
                     EVENT_TRACE_CONTROL_STOP);
        CloseTrace((*EtwData)->ParserHandle);
        HeapFree(GetProcessHeap(), 0, (*EtwData)->Properties);
        HeapFree(GetProcessHeap(), 0, *EtwData);
        return FALSE;
    }

    //
    // Remember which work routine we'll be looking for
    //
    (*EtwData)->WorkItemRoutine = WorkItemRoutine;
    return TRUE;
}
