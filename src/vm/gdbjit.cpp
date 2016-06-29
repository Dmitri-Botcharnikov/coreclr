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
                   TADDR address,
                   ULONG32* numMap,
                   DebuggerILToNativeMap** map,
                   bool* mapAllocated,
                   CLRDATA_ADDRESS* codeStart,
                   ULONG32* codeOffset)
{
    _ASSERTE((codeOffset == NULL) || (address != NULL));

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

    // Update varion out params.
    if (codeStart)
    {
        *codeStart = nativeCodeStartAddr;
    }
    if (codeOffset)
    {
        *codeOffset = (ULONG32)(address - nativeCodeStartAddr);
    }

    *mapAllocated = true;
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

struct my_line_info {
    const char* filepath;
    int line, ilOsset, offset;
} lines[] = {
    { "hello3.cs", 8, 0, 55},
    { "hello3.cs", 9, 1, 56},
    { "hello3.cs", 10, 7, 73},
    { "hello3.cs", 11, 13, 90},
    { "hello3.cs", 12, 31, 130}
};


#define DEBUG_LINE  0x129
#define TEXT_SECTION (0x4b0 + 64)


void NotifyGdb::MethodCompiled(MethodDesc* MethodDescPtr)
{
    printf("NotifyGdb::MethodCompiled %p\n", MethodDescPtr);
    PCODE pCode = MethodDescPtr->GetNativeCode();
    unsigned long line = 0;
    DebuggerILToNativeMap* map = NULL;
    bool mapAllocated = false;

    ULONG32 numMap;
    CLRDATA_ADDRESS codeStart;

    GetMethodNativeMap(MethodDescPtr, 0, &numMap, &map, &mapAllocated,
                       &codeStart, NULL);
    const Module *mod = MethodDescPtr->GetMethodTable()->GetModule();
    SString modName = mod->GetFile()->GetPath();
    StackScratchBuffer scratch;
    const char *szModName = modName.GetUTF8(scratch);
    for (ULONG32 i = 0; i < numMap; i++)
    {
        if (map[i].ilOffset != ICorDebugInfo::NO_MAPPING &&
            map[i].ilOffset != ICorDebugInfo::PROLOG &&
            map[i].ilOffset != ICorDebugInfo::EPILOG)
        {
            BSTR source = SysAllocStringLen(0, 1024);

            getLineByILOffsetDelegate(szModName,
                                      MethodDescPtr->GetMemberDef(),
                                      map[i].ilOffset,
                                      &line,
                                      &source);
            if (SysStringLen(source) > 0)
            {
                printf("IL offsets: %p\t", map[i].ilOffset);
                printf("Source: %S @ %d\n", source, line);
            }
            SysFreeString(source);
        }
    }

    if (pCode == NULL)
        return;
    printf("Native code start: %p\n", pCode);


    jit_code_entry* jit_symbols = new jit_code_entry;
    jit_symbols->next_entry = jit_symbols->prev_entry = 0;
    //jit_symbols->symfile_addr = array;
    //jit_symbols->symfile_size = sizeof(array);
    // __jit_debug_descriptor.first_entry = __jit_debug_descriptor.relevant_entry = jit_symbols;
    // __jit_debug_descriptor.action_flag = JIT_REGISTER_FN;
    // __jit_debug_register_code();



}

void NotifyGdb::MethodDropped(MethodDesc* MethodDescPtr)
{
    printf("NotifyGdb::MethodDropped %p\n", MethodDescPtr);
}

