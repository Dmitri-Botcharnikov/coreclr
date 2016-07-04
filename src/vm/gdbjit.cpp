// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
//*****************************************************************************
// File: gdbjit.cpp
//

//
// NotifyGdb implementation.
//
//*****************************************************************************

#include <stdio.h>
#include "common.h"
#include "../coreclr/hosts/inc/coreclrhost.h"
#include "gdbjit.h"
#include <elf.h>
#include <dwarf.h>

struct DebuggerILToNativeMap
{
    ULONG ilOffset;
    ULONG nativeStartOffset;
    ULONG nativeEndOffset;
    ICorDebugInfo::SourceTypes source;
};
BYTE* DebugInfoStoreNew(void * pData, size_t cBytes)
{
    return new (nothrow) BYTE[cBytes];
}

HRESULT
GetMethodNativeMap(MethodDesc* methodDesc,
                   ULONG32* numMap,
                   DebuggerILToNativeMap** map)
{
    // Use the DebugInfoStore to get IL->Native maps.
    // It doesn't matter whether we're jitted, ngenned etc.

    DebugInfoRequest request;
    TADDR nativeCodeStartAddr = PCODEToPINSTR(methodDesc->GetNativeCode());
    request.InitFromStartingAddr(methodDesc, nativeCodeStartAddr);

    // Bounds info.
    ULONG32 countMapCopy;
    NewHolder<ICorDebugInfo::OffsetMapping> mapCopy(NULL);

    BOOL success = DebugInfoManager::GetBoundariesAndVars(request,
                                                          DebugInfoStoreNew,
                                                          NULL, // allocator
                                                          &countMapCopy,
                                                          &mapCopy,
                                                          NULL,
                                                          NULL);

    if (!success)
    {
        return E_FAIL;
    }

    // Need to convert map formats.
    *numMap = countMapCopy;

    *map = new (nothrow) DebuggerILToNativeMap[countMapCopy];
    if (!*map)
    {
        return E_OUTOFMEMORY;
    }

    ULONG32 i;
    for (i = 0; i < *numMap; i++)
    {
        (*map)[i].ilOffset = mapCopy[i].ilOffset;
        (*map)[i].nativeStartOffset = mapCopy[i].nativeOffset;
        if (i > 0)
        {
            (*map)[i - 1].nativeEndOffset = (*map)[i].nativeStartOffset;
        }
        (*map)[i].source = mapCopy[i].source;
    }
    if (*numMap >= 1)
    {
        (*map)[i - 1].nativeEndOffset = 0;
    }
    return S_OK;
}

struct SymbolsInfo
{
    int lineNumber, ilOffset, nativeOffset;
    char16_t fileName[MAX_PATH_FNAME];
};

HRESULT
GetDebugInfoFromPDB(MethodDesc* MethodDescPtr, SymbolsInfo** symInfo, unsigned int &symInfoLen)
{
    DebuggerILToNativeMap* map = NULL;

    ULONG32 numMap;

    if (GetMethodNativeMap(MethodDescPtr, &numMap, &map) != S_OK)
        return E_FAIL;

    const Module* mod = MethodDescPtr->GetMethodTable()->GetModule();
    SString modName = mod->GetFile()->GetPath();
    StackScratchBuffer scratch;
    const char* szModName = modName.GetUTF8(scratch);

    MethodDebugInfo *methodDebugInfo = new (nothrow )MethodDebugInfo();
    if (methodDebugInfo == nullptr)
        return E_OUTOFMEMORY;
    methodDebugInfo->points = (SequencePointInfo*) CoTaskMemAlloc(sizeof(SequencePointInfo) * numMap);
    if (methodDebugInfo->points == nullptr)
        return E_OUTOFMEMORY;
    methodDebugInfo->size = numMap;
    getInfoForMethodDelegate(szModName, MethodDescPtr->GetMemberDef(), *methodDebugInfo);

    symInfoLen = methodDebugInfo->size;
    *symInfo = new (nothrow) SymbolsInfo[symInfoLen];
    if (*symInfo == nullptr)
        return E_FAIL;

    for (ULONG32 i = 0; i < symInfoLen; i++)
    {
        for (ULONG32 j = 0; j < numMap; j++)
        {
            if (methodDebugInfo->points[i].ilOffset == map[j].ilOffset)
            {
                (*symInfo)[i].nativeOffset = map[j].nativeStartOffset;
                (*symInfo)[i].ilOffset = map[j].ilOffset;
                wcscpy((*symInfo)[i].fileName, methodDebugInfo->points[i].fileName);
                (*symInfo)[i].lineNumber = methodDebugInfo->points[i].lineNumber;
            }
        }
    }

    CoTaskMemFree(methodDebugInfo->points);
    return S_OK;
}

// GDB JIT interface
typedef enum
{
  JIT_NOACTION = 0,
  JIT_REGISTER_FN,
  JIT_UNREGISTER_FN
} jit_actions_t;

struct jit_code_entry
{
  struct jit_code_entry *next_entry;
  struct jit_code_entry *prev_entry;
  const char *symfile_addr;
  UINT64 symfile_size;
};

struct jit_descriptor
{
  UINT32 version;
  /* This type should be jit_actions_t, but we use uint32_t
     to be explicit about the bitwidth.  */
  UINT32 action_flag;
  struct jit_code_entry *relevant_entry;
  struct jit_code_entry *first_entry;
};
/* GDB puts a breakpoint in this function.  */
extern "C"
void __attribute__((noinline)) __jit_debug_register_code() { };

/* Make sure to specify the version statically, because the
   debugger may check the version before we can set it.  */
struct jit_descriptor __jit_debug_descriptor = { 1, 0, 0, 0 };

// END of GDB JIT interface

//  symInfo[] = {
//     { "hello3.cs", 8, 0, 55},
//     { "hello3.cs", 9, 1, 56},
//     { "hello3.cs", 10, 7, 73},
//     { "hello3.cs", 11, 13, 90},
//     { "hello3.cs", 12, 31, 130}
// };


#define DEBUG_LINE  0x129
#define TEXT_SECTION (0x4b0 + 64)


void NotifyGdb::MethodCompiled(MethodDesc* MethodDescPtr)
{
    printf("NotifyGdb::MethodCompiled %p\n", MethodDescPtr);
    PCODE pCode = MethodDescPtr->GetNativeCode();

    if (pCode == NULL)
        return;
    printf("Native code start: %p\n", pCode);
    unsigned int symInfoLen = 0;
    SymbolsInfo* symInfo = nullptr;

    HRESULT hr = GetDebugInfoFromPDB(MethodDescPtr, &symInfo, symInfoLen);
    if (symInfoLen == 0)
    {
        printf("Can't get debug info from portable PDB.\n");
        return;
    }
    for (unsigned int i = 0; i < symInfoLen; i++)
    {
        printf("Native offset: %d  Source: %S Line: %d\n", symInfo[i].nativeOffset, symInfo[i].fileName, symInfo[i].lineNumber);
    }
    delete[] symInfo;

    jit_code_entry* jit_symbols = new jit_code_entry;
    jit_symbols->next_entry = jit_symbols->prev_entry = 0;
    //jit_symbols->symfilefileNamey;
    //jit_symbols->symfile_size = sizeof(array);
    // __jit_debug_descriptor.first_entry = __jit_debug_descriptor.relevant_entry = jit_symbols;
    // __jit_debug_descriptor.action_flag = JIT_REGISTER_FN;
    // __jit_debug_register_code();

}

void NotifyGdb::MethodDropped(MethodDesc* MethodDescPtr)
{
    printf("NotifyGdb::MethodDropped %p\n", MethodDescPtr);
}

