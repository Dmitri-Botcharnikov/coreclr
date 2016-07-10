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

HRESULT
GetDebugInfoFromPDB(MethodDesc* MethodDescPtr, SymbolsInfo** symInfo, unsigned int &symInfoLen)
{
    DebuggerILToNativeMap* map = NULL;

    ULONG32 numMap;

    if (!getInfoForMethodDelegate)
        return E_FAIL;
    
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

    if (!getInfoForMethodDelegate(szModName, MethodDescPtr->GetMemberDef(), *methodDebugInfo))
        return E_FAIL;

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
                SymbolsInfo& s = (*symInfo)[i];
                const SequencePointInfo& sp = methodDebugInfo->points[i];

                s.nativeOffset = map[j].nativeStartOffset;
                s.ilOffset = map[j].ilOffset;
                s.fileIndex = 0;
                //wcscpy(s.fileName, sp.fileName);
                int len = WideCharToMultiByte(CP_UTF8, 0, sp.fileName, -1, s.fileName, sizeof(s.fileName), NULL, NULL);
                s.fileName[len] = 0;
                s.lineNumber = sp.lineNumber;
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


const char* SectionNames[] = {
    "", ".text", ".shstrtab", ".debug_str", ".debug_abbrev", ".debug_info",
    ".debug_pubnames", ".debug_pubtypes", ".debug_line", ""
};

const int SectionNamesCount = sizeof(SectionNames) / sizeof(SectionNames[0]);

struct SectionHeader {
    uint32_t m_type;
    uint64_t m_flags;
} Sections[] = {
    {SHT_NULL, 0},
    {SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR},
    {SHT_STRTAB, 0},
    {SHT_PROGBITS, SHF_MERGE | SHF_STRINGS },
    {SHT_PROGBITS, 0},
    {SHT_PROGBITS, 0},
    {SHT_PROGBITS, 0},
    {SHT_PROGBITS, 0},
    {SHT_PROGBITS, 0}
};

const char* DebugStrings[] = {
    "CoreCLR", "" /* module name */, "" /* module path */, "" /* method name */, "int"
};

const int DebugStringCount = sizeof(DebugStrings) / sizeof(DebugStrings[0]);

const unsigned char AbbrevTable[] = {
    1, DW_TAG_compile_unit, DW_children_yes,
        DW_AT_producer, DW_FORM_strp, DW_AT_language, DW_FORM_data2, DW_AT_name, DW_FORM_strp,
        DW_AT_stmt_list, DW_FORM_sec_offset, 0, 0,
    2, DW_TAG_subprogram, DW_children_no,
        DW_AT_name, DW_FORM_strp, DW_AT_decl_file, DW_FORM_data1, DW_AT_decl_line, DW_FORM_data1,
        DW_AT_type, DW_FORM_ref4, DW_AT_external, DW_FORM_flag_present, 0, 0,
    3, DW_TAG_base_type, DW_children_no,
        DW_AT_name, DW_FORM_strp, DW_AT_encoding, DW_FORM_data1, DW_AT_byte_size, DW_FORM_data1,0, 0,
    0
};

const int AbbrevTableSize = sizeof(AbbrevTable);

#define DWARF_LINE_BASE (-5)
#define DWARF_LINE_RANGE 14
#define DWARF_OPCODE_BASE 13

DwarfLineNumHeader LineNumHeader = {
    0, 2, 0, 1, 1, DWARF_LINE_BASE, DWARF_LINE_RANGE, DWARF_OPCODE_BASE, {0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1}
};

struct __attribute__((packed)) DebugInfo
{
    uint8_t m_cu_abbrev;
    uint32_t m_prod_off;
    uint16_t m_lang;
    uint32_t m_cu_name;
    uint32_t m_line_num;
    
    uint8_t m_sub_abbrev;
    uint32_t m_sub_name;
    uint8_t m_file, m_line;
    uint32_t m_sub_type;
    
    uint8_t m_type_abbrev;
    uint32_t m_type_name;
    uint8_t m_encoding;
    uint8_t m_byte_size;
} debugInfo = {
    1, 0, DW_LANG_C89, 0, 0,
    2, 0, 1, 1, 37,
    3, 0, DW_ATE_signed, 4
};

void NotifyGdb::MethodCompiled(MethodDesc* MethodDescPtr)
{
    printf("NotifyGdb::MethodCompiled %p\n", MethodDescPtr);
    PCODE pCode = MethodDescPtr->GetNativeCode();

    if (pCode == NULL)
        return;
    printf("Native code start: %p\n", pCode);
    unsigned int symInfoLen = 0;
    SymbolsInfo* symInfo = nullptr;

    LPCUTF8 methodName = MethodDescPtr->GetName();
    printf("Method name: %s\n", methodName);
    EECodeInfo codeInfo(pCode);
    TADDR codeSize = codeInfo.GetCodeManager()->GetFunctionSize(codeInfo.GetGCInfo());
    printf("Code size: %d\n", codeSize);

    const Module* mod = MethodDescPtr->GetMethodTable()->GetModule();
    SString modName = mod->GetFile()->GetPath();
    StackScratchBuffer scratch;
    const char* szModName = modName.GetUTF8(scratch);
    printf("Module name: %s\n", szModName);
    const char *szModulePath, *szModuleFile;
    
    SplitPathname(szModName, szModulePath, szModuleFile);
    printf("Module path: %s, filename: %s\n", szModulePath, szModuleFile);
    
    
    HRESULT hr = GetDebugInfoFromPDB(MethodDescPtr, &symInfo, symInfoLen);
    if (FAILED(hr) || symInfoLen == 0)
    {
        printf("Can't get debug info from portable PDB.\n");
        return;
    }
    for (unsigned int i = 0; i < symInfoLen; i++)
    {
        printf("Native offset: %d  Source: %s Line: %d\n", symInfo[i].nativeOffset, symInfo[i].fileName, symInfo[i].lineNumber);
    }


    
    MemBuf elfHeader, sectHeaders, sectStr, dbgInfo, dbgAbbrev, dbgPubname, dbgPubType, dbgLine, dbgStr, elfFile;

    if (!BuildDebugAbbrev(dbgAbbrev))
        return;
    
    if (!BuildLineTable(dbgLine, pCode, symInfo, symInfoLen))
        return;
    
    DebugStrings[1] = szModuleFile;
    DebugStrings[3] = methodName;
    
    if (!BuildDebugStrings(dbgStr))
        return;
    
    if (!BuildDebugInfo(dbgInfo))
        return;
    
    if (!BuildDebugPub(dbgPubname, methodName, dbgInfo.MemSize, 26))
        return;
    
    if (!BuildDebugPub(dbgPubType, "int", dbgInfo.MemSize, 37))
        return;
    
    if (!BuildSectionNameTable(sectStr))
        return;

    if (!BuildSectionTable(sectHeaders))
        return;
    long offset = sizeof(Elf_Ehdr);
    Elf_Shdr* pShdr = reinterpret_cast<Elf_Shdr*>(sectHeaders.MemPtr);
    ++pShdr; // .text
    pShdr->sh_addr = pCode;
    pShdr->sh_size = codeSize;
    ++pShdr; // .shstrtab
    pShdr->sh_offset = offset;
    pShdr->sh_size = sectStr.MemSize;
    offset += sectStr.MemSize;
    ++pShdr; // .debug_str
    pShdr->sh_offset = offset;
    pShdr->sh_size = dbgStr.MemSize;
    offset += dbgStr.MemSize;
    ++pShdr; // .debug_abbrev
    pShdr->sh_offset = offset;
    pShdr->sh_size = dbgAbbrev.MemSize;
    offset += dbgAbbrev.MemSize;
    ++pShdr; // .debug_info
    pShdr->sh_offset = offset;
    pShdr->sh_size = dbgInfo.MemSize;
    offset += dbgInfo.MemSize;
    ++pShdr; // .debug_pubnames
    pShdr->sh_offset = offset;
    pShdr->sh_size = dbgPubname.MemSize;
    offset += dbgPubname.MemSize;
    ++pShdr; // .debug_pubtypes
    pShdr->sh_offset = offset;
    pShdr->sh_size = dbgPubType.MemSize;
    offset += dbgPubType.MemSize;
    ++pShdr; // .debug_line
    pShdr->sh_offset = offset;
    pShdr->sh_size = dbgLine.MemSize;
    offset += dbgLine.MemSize;
    
    
    if (!BuildELFHeader(elfHeader))
        return;
    Elf_Ehdr* header = reinterpret_cast<Elf_Ehdr*>(elfHeader.MemPtr);
#if defined(_TARGET_ARM_)
    header->e_flags = EF_ARM_EABI;
#endif
    header->e_shoff = offset;
    header->e_shentsize = sizeof(Elf_Shdr);
    header->e_shnum = SectionNamesCount - 1;
    header->e_shstrndx = 2;

    
    elfFile.MemSize = elfHeader.MemSize + sectStr.MemSize + dbgStr.MemSize + dbgAbbrev.MemSize
                        + dbgInfo.MemSize + dbgPubname.MemSize + dbgPubType.MemSize + dbgLine.MemSize + sectHeaders.MemSize;
    elfFile.MemPtr =  new char[elfFile.MemSize];
    
    printf("Elf file @%p:%d\n", elfFile.MemPtr, elfFile.MemSize);
    
    offset = 0;
    memcpy(elfFile.MemPtr, elfHeader.MemPtr, elfHeader.MemSize);
    offset += elfHeader.MemSize;
    memcpy(elfFile.MemPtr + offset, sectStr.MemPtr, sectStr.MemSize);
    offset +=  sectStr.MemSize;
    memcpy(elfFile.MemPtr + offset, dbgStr.MemPtr, dbgStr.MemSize);
    offset +=  dbgStr.MemSize;
    memcpy(elfFile.MemPtr + offset, dbgAbbrev.MemPtr, dbgAbbrev.MemSize);
    offset +=  dbgAbbrev.MemSize;
    memcpy(elfFile.MemPtr + offset, dbgInfo.MemPtr, dbgInfo.MemSize);
    offset +=  dbgInfo.MemSize;
    memcpy(elfFile.MemPtr + offset, dbgPubname.MemPtr, dbgPubname.MemSize);
    offset +=  dbgPubname.MemSize;
    memcpy(elfFile.MemPtr + offset, dbgPubType.MemPtr, dbgPubType.MemSize);
    offset +=  dbgPubType.MemSize;
    memcpy(elfFile.MemPtr + offset, dbgLine.MemPtr, dbgLine.MemSize);
    offset +=  dbgLine.MemSize;
    memcpy(elfFile.MemPtr + offset, sectHeaders.MemPtr, sectHeaders.MemSize);
    
    delete[] symInfo;
    delete[] elfHeader.MemPtr;
    delete[] sectStr.MemPtr;
    delete[] dbgStr.MemPtr;
    delete[] dbgAbbrev.MemPtr;
    delete[] dbgInfo.MemPtr;
    delete[] dbgPubname.MemPtr;
    delete[] dbgPubType.MemPtr;
    delete[] dbgLine.MemPtr;
    delete[] sectHeaders.MemPtr;
    
    jit_code_entry* jit_symbols = new jit_code_entry;
    jit_symbols->next_entry = jit_symbols->prev_entry = 0;
    jit_symbols->symfile_addr = elfFile.MemPtr;
    jit_symbols->symfile_size = elfFile.MemSize;
    __jit_debug_descriptor.first_entry = __jit_debug_descriptor.relevant_entry = jit_symbols;
    __jit_debug_descriptor.action_flag = JIT_REGISTER_FN;
    __jit_debug_register_code();

}

void NotifyGdb::MethodDropped(MethodDesc* MethodDescPtr)
{
    printf("NotifyGdb::MethodDropped %p\n", MethodDescPtr);
}

bool NotifyGdb::BuildLineTable(MemBuf& buf, PCODE startAddr, SymbolsInfo* lines, unsigned nlines)
{
    MemBuf fileTable, lineProg;
    
    BuildFileTable(fileTable, lines, nlines);
    BuildLineProg(lineProg, startAddr, lines, nlines);
    
    buf.MemSize = sizeof(DwarfLineNumHeader) + 1 + fileTable.MemSize + lineProg.MemSize;
    buf.MemPtr = new char[buf.MemSize];
    
    
    DwarfLineNumHeader* header = reinterpret_cast<DwarfLineNumHeader*>(buf.MemPtr);
    memcpy(buf.MemPtr, &LineNumHeader, sizeof(DwarfLineNumHeader));
    header->m_length = buf.MemSize - sizeof(uint32_t);
    header->m_hdr_length = sizeof(DwarfLineNumHeader) + 1 + fileTable.MemSize - 2 * sizeof(uint32_t) - sizeof(uint16_t);
    buf.MemPtr[sizeof(DwarfLineNumHeader)] = 0;
    memcpy(buf.MemPtr + sizeof(DwarfLineNumHeader) + 1, fileTable.MemPtr, fileTable.MemSize);
    memcpy(buf.MemPtr + sizeof(DwarfLineNumHeader) + 1 + fileTable.MemSize, lineProg.MemPtr, lineProg.MemSize);
    
    delete[] fileTable.MemPtr;
    delete[] lineProg.MemPtr;
    
    return true;
}

void NotifyGdb::BuildFileTable(MemBuf& buf, SymbolsInfo* lines, unsigned nlines)
{
    const char** files = nullptr;
    unsigned nfiles = 0;
    
    files = new const char*[nlines];
    for (unsigned i = 0; i < nlines; ++i)
    {
        const char *filePath, *fileName;
        SplitPathname(lines[i].fileName, filePath, fileName);
        
        lines[i].fileIndex = nfiles;
        
        bool found = false;
        for (int j = 0; j < nfiles; ++j)
        {
            if (strcmp(fileName, files[j]) == 0)
            {
                found = true;
                break;
            }
        }
        
        if (!found)
        {
            files[nfiles++] = fileName;
        }
    }
    
    unsigned totalSize = 0;
    
    for (unsigned i = 0; i < nfiles; ++i)
    {
        totalSize += strlen(files[i]) + 1 + 3;
    }
    totalSize += 1;
    
    buf.MemSize = totalSize;
    buf.MemPtr = new char[buf.MemSize];
    
    char *ptr = buf.MemPtr;
    for (unsigned i = 0; i < nfiles; ++i)
    {
        strcpy(ptr, files[i]);
        ptr += strlen(files[i]) + 1;
        *ptr++ = 0;
        *ptr++ = 0;
        *ptr++ = 0;
    }
    *ptr = 0;

    delete[] files;
}

void NotifyGdb::IssueSetAddress(char*& ptr, PCODE addr)
{
    *ptr++ = 0;
    *ptr++ = ADDRESS_SIZE + 1;
    *ptr++ = DW_LNE_set_address;
    *reinterpret_cast<PCODE*>(ptr) = addr;
    ptr += ADDRESS_SIZE;
}

void NotifyGdb::IssueEndOfSequence(char*& ptr)
{
    *ptr++ = 0;
    *ptr++ = 1;
    *ptr++ = DW_LNE_end_sequence;
}

void NotifyGdb::IssueSimpleCommand(char*& ptr, uint8_t command)
{
    *ptr++ = command;
}

void NotifyGdb::IssueParamCommand(char*& ptr, uint8_t command, uint8_t param)
{
    *ptr++ = command;
    *ptr++ = param;
}

void NotifyGdb::IssueSpecialCommand(char*& ptr, int8_t line_shift, uint8_t addr_shift)
{
    *ptr++ = (line_shift - DWARF_LINE_BASE) + addr_shift * DWARF_LINE_RANGE + DWARF_OPCODE_BASE;
}

bool NotifyGdb::FitIntoSpecialOpcode(int8_t line_shift, uint8_t addr_shift)
{
    unsigned opcode = (line_shift - DWARF_LINE_BASE) + addr_shift * DWARF_LINE_RANGE + DWARF_OPCODE_BASE;
    
    return opcode < 255;
}

void NotifyGdb::BuildLineProg(MemBuf& buf, PCODE startAddr, SymbolsInfo* lines, unsigned nlines)
{
    buf.MemSize = nlines * ( 4 + ADDRESS_SIZE) + 4;
    buf.MemPtr = new char[buf.MemSize];
    char* ptr = buf.MemPtr;
    
    IssueSetAddress(ptr, startAddr);
    IssueSimpleCommand(ptr, DW_LNS_set_prologue_end);
    
    int prevLine = 1, prevAddr = 0;
    
    for (int i = 0; i < nlines; ++i)
    {
       if (lines[i].lineNumber - prevLine > (DWARF_LINE_BASE + DWARF_LINE_RANGE - 1))
       {
           IssueParamCommand(ptr, DW_LNS_advance_line, lines[i].lineNumber - prevLine);
           prevLine = lines[i].lineNumber;
       }
       if (FitIntoSpecialOpcode(lines[i].lineNumber - prevLine, lines[i].nativeOffset - prevAddr))
           IssueSpecialCommand(ptr, lines[i].lineNumber - prevLine, lines[i].nativeOffset - prevAddr);
       else
       {
           IssueSetAddress(ptr, startAddr + lines[i].nativeOffset);
           IssueSpecialCommand(ptr, lines[i].lineNumber - prevLine, 0);
       }
           
       prevLine = lines[i].lineNumber;
       prevAddr = lines[i].nativeOffset;
    }
    
    IssueEndOfSequence(ptr); 
    
    buf.MemSize = ptr - buf.MemPtr;
}

bool NotifyGdb::BuildDebugStrings(MemBuf& buf)
{
    uint32_t totalLength = 0;
    
    for (int i = 0; i < DebugStringCount; ++i)
    {
        totalLength += strlen(DebugStrings[i]) + 1;
    }
    
    buf.MemSize = totalLength;
    buf.MemPtr = new char[totalLength];

    char* bufPtr = buf.MemPtr;
    for (int i = 0; i < DebugStringCount; ++i)
    {
        strcpy(bufPtr, DebugStrings[i]);
        bufPtr += strlen(DebugStrings[i]) + 1;
    }
    
    return true;
}

bool NotifyGdb::BuildDebugAbbrev(MemBuf& buf)
{
    buf.MemPtr = new char[AbbrevTableSize];
    buf.MemSize = AbbrevTableSize;
    memcpy(buf.MemPtr, AbbrevTable, AbbrevTableSize);
    return true;
}

bool NotifyGdb::BuildDebugInfo(MemBuf& buf)
{
    buf.MemSize = sizeof(DwarfCompUnit) + sizeof(DebugInfo) + 1;
    buf.MemPtr = new char[buf.MemSize];
    
    DwarfCompUnit* cu = reinterpret_cast<DwarfCompUnit*>(buf.MemPtr);
    cu->m_length = buf.MemSize - sizeof(uint32_t);
    cu->m_version = 4;
    cu->m_abbrev_offset = 0;
    cu->m_addr_size = ADDRESS_SIZE;
    
    DebugInfo* di = reinterpret_cast<DebugInfo*>(buf.MemPtr + sizeof(DwarfCompUnit));
    memcpy(buf.MemPtr + sizeof(DwarfCompUnit), &debugInfo, sizeof(DebugInfo));
    di->m_prod_off = 0;
    di->m_cu_name = strlen(DebugStrings[0]) + 1;
    di->m_sub_name = strlen(DebugStrings[0]) + 1 + strlen(DebugStrings[1]) + 1 + strlen(DebugStrings[2]) + 1;
    di->m_type_name = strlen(DebugStrings[0]) + 1 + strlen(DebugStrings[1]) + 1 + strlen(DebugStrings[2]) + 1 + strlen(DebugStrings[3]) + 1;
    
    buf.MemPtr[buf.MemSize-1] = 0;
    return true;
}

bool NotifyGdb::BuildDebugPub(MemBuf& buf, const char* name, uint32_t size, uint32_t die_offset)
{
    uint32_t length = sizeof(DwarfPubHeader) + sizeof(uint32_t) + strlen(name) + 1 + sizeof(uint32_t);
    
    buf.MemSize = length;
    buf.MemPtr = new char[buf.MemSize];
    
    DwarfPubHeader* header = reinterpret_cast<DwarfPubHeader*>(buf.MemPtr);
    header->m_length = length - sizeof(uint32_t);
    header->m_version = 2;
    header->m_debug_info_off = 0;
    header->m_debug_info_len = size;
    *reinterpret_cast<uint32_t*>(buf.MemPtr + sizeof(DwarfPubHeader)) = die_offset;
    strcpy(buf.MemPtr + sizeof(DwarfPubHeader) + sizeof(uint32_t), name);
    *reinterpret_cast<uint32_t*>(buf.MemPtr + length - sizeof(uint32_t)) = 0;
    
    return true;
}

bool NotifyGdb::BuildSectionNameTable(MemBuf& buf)
{
    uint32_t totalLength = 0;
    
    for (int i = 0; i < SectionNamesCount; ++i)
    {
        totalLength += strlen(SectionNames[i]) + 1;
    }
    
    buf.MemSize = totalLength;
    buf.MemPtr = new char[totalLength];

    char* bufPtr = buf.MemPtr;
    for (int i = 0; i < SectionNamesCount; ++i)
    {
        strcpy(bufPtr, SectionNames[i]);
        bufPtr += strlen(SectionNames[i]) + 1;
    }
    
    return true;
}

bool NotifyGdb::BuildSectionTable(MemBuf& buf)
{
    Elf_Shdr* sectionHeaders = new Elf_Shdr[SectionNamesCount - 1];    
    Elf_Shdr* pSh = sectionHeaders;
    
    pSh->sh_name = 0;
    pSh->sh_type = SHT_NULL;
    pSh->sh_flags = 0;
    pSh->sh_addr = 0;
    pSh->sh_offset = 0;
    pSh->sh_size = 0;
    pSh->sh_link = SHN_UNDEF;
    pSh->sh_info = 0;
    pSh->sh_addralign = 0;
    pSh->sh_entsize = 0;
    
    ++pSh;
    uint32_t sectNameOffset = 1;
    for( int i = 1; i < SectionNamesCount - 1; ++i, ++pSh)
    {
        pSh->sh_name = sectNameOffset;
        sectNameOffset += strlen(SectionNames[i]) + 1;
        pSh->sh_type = Sections[i].m_type;
        pSh->sh_flags = Sections[i].m_flags;
        pSh->sh_addr = 0;
        pSh->sh_offset = 0;
        pSh->sh_size = 0;
        pSh->sh_link = SHN_UNDEF;
        pSh->sh_info = 0;
        pSh->sh_addralign = 1;
        pSh->sh_entsize = 0;
    }

    buf.MemPtr = reinterpret_cast<char*>(sectionHeaders);
    buf.MemSize = sizeof(Elf_Shdr) * (SectionNamesCount - 1);
    return true;
}

bool NotifyGdb::BuildELFHeader(MemBuf& buf)
{
    Elf_Ehdr* header = new Elf_Ehdr;
    buf.MemPtr = reinterpret_cast<char*>(header);
    buf.MemSize = sizeof(Elf_Ehdr);
    
    return true;
        
}

void NotifyGdb::SplitPathname(const char* path, const char*& pathName, const char*& fileName)
{
    char* pSlash = strrchr(path, '/');
    
    if(pSlash != nullptr)
    {
        *pSlash = 0;
        fileName = ++pSlash;
        pathName = path;
    }
    else 
    {
        fileName = path;
        pathName = nullptr;
    }
}

Elf32_Ehdr::Elf32_Ehdr()
{
    e_ident[EI_MAG0] = ELFMAG0;
    e_ident[EI_MAG1] = ELFMAG1;
    e_ident[EI_MAG2] = ELFMAG2;
    e_ident[EI_MAG3] = ELFMAG3;
    e_ident[EI_CLASS] = ELFCLASS32;
    e_ident[EI_DATA] = ELFDATA2LSB;
    e_ident[EI_VERSION] = EV_CURRENT;
    e_ident[EI_OSABI] = ELFOSABI_SYSV;
    e_ident[EI_ABIVERSION] = 0;
    for (int i = EI_PAD; i < EI_NIDENT; ++i)
        e_ident[i] = 0;

    e_type = ET_REL;
#if defined(_TARGET_X86_)
    e_machine = EM_386;
#elif defined(_TARGET_ARM_)
    e_machine = EM_ARM;
#endif    
    e_flags = 0;
    e_version = 1;
    e_entry = 0;
    e_phoff = 0;
    e_ehsize = sizeof(Elf32_Ehdr);
    e_phentsize = 0;
    e_phnum = 0;
}

Elf64_Ehdr::Elf64_Ehdr()
{
    e_ident[EI_MAG0] = ELFMAG0;
    e_ident[EI_MAG1] = ELFMAG1;
    e_ident[EI_MAG2] = ELFMAG2;
    e_ident[EI_MAG3] = ELFMAG3;
    e_ident[EI_CLASS] = ELFCLASS64;
    e_ident[EI_DATA] = ELFDATA2LSB;
    e_ident[EI_VERSION] = EV_CURRENT;
    e_ident[EI_OSABI] = ELFOSABI_SYSV;
    e_ident[EI_ABIVERSION] = 0;
    for (int i = EI_PAD; i < EI_NIDENT; ++i)
        e_ident[i] = 0;

    e_type = ET_REL;
#if defined(_TARGET_AMD64_)
    e_machine = EM_X86_64;
#elif defined(_TARGET_ARM64_)
    e_machine = EM_AARCH64;
#endif
    e_flags = 0;
    e_version = 1;
    e_entry = 0;
    e_phoff = 0;
    e_ehsize = sizeof(Elf64_Ehdr);
    e_phentsize = 0;
    e_phnum = 0;
}
