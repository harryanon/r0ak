/*++

Copyright (c) Alex Ionescu.  All rights reserved.

Module Name:

    r0ak.c

Abstract:

    This module implements the main command line interface for r0ak

Author:

    Alex Ionescu (@aionescu) 21-Jul-2018 - First public version

Environment:

    User mode only.

--*/

#include "r0ak.h"

_Success_(return != 0)
BOOL
CmdParseInputParameters (
    _In_ PCHAR Arguments[],
    _Out_ PVOID* Function,
    _Out_ PULONG_PTR FunctionArgument
    )
{
    PVOID functionPointer;
    PCHAR moduleName, functionName, pBang, functionNameAndModule;

    //
    // Check if the user passed in a module!function instead
    //
    functionPointer = (PVOID)strtoull(Arguments[2], NULL, 0);
    if (functionPointer == NULL)
    {
        //
        // Separate out the module name from the symbol name
        //
        functionNameAndModule = Arguments[2];
        pBang = strchr(functionNameAndModule, '!');
        if (pBang == NULL)
        {
            printf("[-] Malformed symbol string: %s\n",
                   Arguments[2]);
            return FALSE;
        }

        //
        // Now get the remaining function name
        //
        functionName = pBang + 1;
        *pBang = ANSI_NULL;
        moduleName = functionNameAndModule;

        //
        // Get the symbol requested
        //
        functionPointer = SymLookup(moduleName, functionName);
        if (functionPointer == NULL)
        {
            printf("[-] Could not find symbol!\n");
            return FALSE;
        }
    }

    //
    // Return the data back
    //
    *Function = functionPointer;
    *FunctionArgument = strtoull(Arguments[3], NULL, 0);
    return TRUE;
}

INT
main (
    _In_ INT ArgumentCount,
    _In_ PCHAR Arguments[]
    )
{
    PKERNEL_EXECUTE kernelExecute;
    BOOL b;
    ULONG_PTR kernelValue;
    PVOID kernelPointer;
    INT errValue;

    //
    // Print header
    //
    printf("r0ak v1.0.0 -- Ring 0 Army Knife\n");
    printf("http://www.github.com/ionescu007/r0ak\n");
    printf("Copyright (c) 2018 Alex Ionescu [@aionescu]\n");
    printf("http://www.windows-internals.com\n\n");
    kernelExecute = NULL;
    errValue = -1;

    //
    // We need four arguments
    //
    if (ArgumentCount != 4)
    {
        printf("USAGE: r0ak.exe\n"
               "       [--execute <Address | module!function> <Argument>]\n"
               "       [--write   <Address | module!function> <Value>]\n"
               "       [--read    <Address | module!function> <Size>]\n");
        goto Cleanup;
    }

    //
    // Initialize symbol engine
    //
    b = SymSetup();
    if (b == FALSE)
    {
        printf("[-] Failed to initialize Symbol Engine\n");
        goto Cleanup;
    }

    //
    // Initialize our execution engine
    //
    b = KernelExecuteSetup(&kernelExecute, g_TrampolineFunction);
    if (b == FALSE)
    {
        printf("[-] Failed to setup Ring 0 execution engine\n");
        goto Cleanup;
    }

    //
    // Caller wants to execute their own routine
    //
    if (strstr(Arguments[1], "--execute"))
    {
        //
        // Get the initial inputs
        //
        b = CmdParseInputParameters(Arguments, &kernelPointer, &kernelValue);
        if (b == FALSE)
        {
            goto Cleanup;
        }

        //
        // Execute it
        //
        b = CmdExecuteKernel(kernelExecute, kernelPointer, kernelValue);
        if (b == FALSE)
        {
            printf("[-] Failed to execute function\n");
            goto Cleanup;
        }

        //
        // It's now safe to exit/cleanup state
        //
        printf("[+] Function executed successfuly!\n");
        errValue = 0;
    }
    else if (strstr(Arguments[1], "--write"))
    {
        //
        // Get the initial inputs
        //
        b = CmdParseInputParameters(Arguments, &kernelPointer, &kernelValue);
        if (b == FALSE)
        {
            goto Cleanup;
        }

        //
        // Only 32-bit values can be written
        //
        if (kernelValue > ULONG_MAX)
        {
            printf("[-] Invalid 64-bit value, r0ak only supports 32-bit\n");
            goto Cleanup;
        }

        //
        // Write it!
        //
        b = CmdWriteKernel(kernelExecute, kernelPointer, (ULONG)kernelValue);
        if (b == FALSE)
        {
            printf("[-] Failed to write variable\n");
            goto Cleanup;
        }

        //
        // It's now safe to exit/cleanup state
        //
        printf("[+] Write executed successfuly!\n");
        errValue = 0;
    }
    else if (strstr(Arguments[1], "--read"))
    {
        //
        // Get the initial inputs
        //
        b = CmdParseInputParameters(Arguments, &kernelPointer, &kernelValue);
        if (b == FALSE)
        {
            goto Cleanup;
        }

        //
        // Only 4GB of data can be read
        //
        if (kernelValue > ULONG_MAX)
        {
            printf("[-] Invalid size, r0ak can only read up to 4GB of data\n");
            goto Cleanup;
        }


        //
        // Write it!
        //
        b = CmdReadKernel(kernelExecute, kernelPointer, (ULONG)kernelValue);
        if (b == FALSE)
        {
            printf("[-] Failed to read variable\n");
            goto Cleanup;
        }

        //
        // It's now safe to exit/cleanup state
        //
        printf("[+] Read executed successfuly!\n");
        errValue = 0;
    }

Cleanup:
    //
    // Teardown the execution engine if we initialized it
    //
    if (kernelExecute != NULL)
    {
        KernelExecuteTeardown(kernelExecute);
    }
    return errValue;
}
