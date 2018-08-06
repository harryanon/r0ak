/*++

Copyright (c) Alex Ionescu.  All rights reserved.

Module Name:

    r0akutil.c

Abstract:

    This module implements utility functions for r0ak

Author:

    Alex Ionescu (@aionescu) 21-Jul-2018 - First public version

Environment:

    User mode only.

--*/

#include "r0ak.h"

VOID
DumpHex (
    _In_ LPCVOID Data,
    _In_ SIZE_T Size
    )
{
    CHAR ascii[17];
    SIZE_T i, j;

    //
    // Parse each byte in the stream
    //
    ascii[16] = ANSI_NULL;
    for (i = 0; i < Size; ++i)
    {
        //
        // Every new line, print a TAB
        //
        if ((i % 16) == 0)
        {
            printf("\t");
        }

        //
        // Print the hex representation of the data
        //
        printf("%02X", ((PUCHAR)Data)[i]);

        //
        // And as long as this isn't the middle dash, print a space
        //
        if ((i + 1) % 16 != 8)
        {
            printf(" ");
        }
        else
        {
            printf("-");
        }

        //
        // Is this a printable character? If not, use a '.' to represent it
        //
        if (isprint(((PUCHAR)Data)[i]))
        {
            ascii[i % 16] = ((PUCHAR)Data)[i];
        }
        else
        {
            ascii[i % 16] = '.';
        }

        //
        // Is this end of the line? If so, print it out
        //
        if (((i + 1) % 16) == 0)
        {
            printf(" %s\n", ascii);
        }

        if ((i + 1) == Size)
        {
            //
            // We've reached the end of the buffer, keep printing spaces
            // until we get to the end of the line
            //
            ascii[(i + 1) % 16] = ANSI_NULL;
            for (j = ((i + 1) % 16); j < 16; j++)
            {
                printf("   ");
            }

            printf(" %s\n", ascii);
        }
    }
}

_Success_(return != 0)
ULONG_PTR
GetDriverBaseAddr (
    _In_ PCCH BaseName
    )
{
    LPVOID BaseAddresses[1024];
    DWORD cbNeeded;
    CHAR FileName[MAX_PATH];

    //
    // Enumerate all the device drivers
    //
    if (!EnumDeviceDrivers(BaseAddresses, sizeof(BaseAddresses), &cbNeeded))
    {
        printf("[-] Failed to enumerate driver base addresses: %lx\n",
               GetLastError());
        return 0;
    }

    //
    // Go through each one
    //
    for (int i = 0; i < (cbNeeded / sizeof(LPVOID)); i++)
    {
        //
        // Get its name
        //
        if (!GetDeviceDriverBaseNameA(BaseAddresses[i],
                                      FileName,
                                      sizeof(FileName)))
        {
            printf("[-] Failed to get driver name: %lx\n",
                   GetLastError());
            return 0;
        }

        //
        // Compare it
        //
        if (!_stricmp(FileName, BaseName))
        {
            return (ULONG_PTR)BaseAddresses[i];
        }
    }
    return 0;
}

_Success_(return != 0)
BOOL
ElevateToSystem (
    VOID
    )
{
    HANDLE hProcess;
    HANDLE hToken, hNewtoken;
    HANDLE hSnapshot;
    DWORD logonPid;
    BOOL b;
    PROCESSENTRY32W processEntry;
    BOOLEAN old;
    NTSTATUS status;

    //
    // Create toolhelp snaapshot
    //
    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == NULL)
    {
        printf("[-] Failed to initialize toolhelp snapshot: %lx\n",
               GetLastError());
        return FALSE;
    }

    //
    // Scan process list
    //
    logonPid = 0;
    processEntry.dwSize = sizeof(processEntry);
    Process32First(hSnapshot, &processEntry);
    do
    {
        //
        // Look for winlogon
        //
        if (wcsstr(processEntry.szExeFile, L"winlogon.exe") != NULL)
        {
            //
            // Found it
            //
            logonPid = processEntry.th32ProcessID;
            break;
        }
    } while (Process32Next(hSnapshot, &processEntry) != 0);

    //
    // Fail it not found
    //
    if (logonPid == 0)
    {
        printf("[-] Couldn't find Winlogon.exe\n");
        return FALSE;
    }

    //
    // Enable debug privileges, so that we may open the processes we need
    //
    status = RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, TRUE, FALSE, &old);
    if (!NT_SUCCESS(status))
    {
        printf("[-] Failed to get SE_DEBUG_PRIVILEGE: %lx\n",
               status);
        return FALSE;
    }

    //
    // Open handle to it
    //
    hProcess = OpenProcess(MAXIMUM_ALLOWED, FALSE, logonPid);
    if (hProcess == NULL)
    {
        printf("[-] Failed to open handle to Winlogon: %lx\n",
               GetLastError());
        return FALSE;
    }

    //
    // Open winlogon's token
    //
    b = OpenProcessToken(hProcess, MAXIMUM_ALLOWED, &hToken);
    if (b == 0)
    {
        printf("[-] Failed to open Winlogon Token: %lx\n",
               GetLastError());
        return b;
    }

    //
    // Make an impersonation token copy out of it
    //
    b = DuplicateToken(hToken, SecurityImpersonation, &hNewtoken);
    if (b == 0)
    {
        printf("[-] Failed to duplicate Winlogon Token: %lx\n",
               GetLastError());
        return b;
    }

    //
    // And assign it as our thread token
    //
    b = SetThreadToken(NULL, hNewtoken);
    if (b == 0)
    {
        printf("[-] Failed to impersonate Winlogon Token: %lx\n",
               GetLastError());
        return b;
    }

    //
    // Close the handle to wininit, its token, and our copy
    //
    CloseHandle(hProcess);
    CloseHandle(hToken);
    CloseHandle(hNewtoken);
    return TRUE;
}
