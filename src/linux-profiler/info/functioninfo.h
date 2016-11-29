#ifndef _FUNCTION_INFO_
#define _FUNCTION_INFO_

#include <string>
#include <vector>

#include <cor.h>
#include <corhdr.h>
#include <corprof.h>

#include "classinfo.h"
#include "mappedinfo.h"

class Profiler;

class ProfilerInfo;

class ClassStorage;

class ExecutionTrace;

struct FunctionInfo : public MappedInfo<FunctionID>
{
    typedef std::basic_string<WCHAR> String;

    ExecutionTrace *executionTrace;
    std::vector<COR_PRF_CODE_INFO> codeInfo;
    std::vector<COR_DEBUG_IL_TO_NATIVE_MAP> ILToNativeMapping;
    ModuleID    moduleId;
    ModuleID    classId;
    mdMethodDef funcToken = mdMethodDefNil;
    ClassInfo*  ownerClass;
    std::vector<ClassInfo*> typeArgs;
    String      name;
    String      fullName;
    String      returnType;
    String      signature;
    bool        isInitialized;
    bool        isNamePrinted;

private:

    static void ParseElementType(
        const Profiler &profiler,
        IMetaDataImport *pMDImport,
        PCCOR_SIGNATURE &sigBlob,
        ULONG &sigBlobSize,
        String &str,
        String &arrayBrackets);

    static void ParseSignature(
        const Profiler &profiler,
        IMetaDataImport *pMDImport,
        PCCOR_SIGNATURE &sigBlob,
        ULONG &sigBlobSize,
        String &returnType,
        String &signature);

    HRESULT InitializeCodeInfo(
        const Profiler &profiler,
        const ProfilerInfo &info) noexcept;

    HRESULT InitializeILToNativeMapping(
        const Profiler &profiler,
        const ProfilerInfo &info) noexcept;

    HRESULT InitializeFunctionName(
        const Profiler &profiler,
        IMetaDataImport *pMDImport,
        ULONG funcNameSize) noexcept;

    HRESULT InitializeOwnerClassFromClassId(
        const Profiler &profiler,
        ClassStorage &storage) noexcept;

    HRESULT InitializeOwnerClassFromClassToken(
        const Profiler &profiler,
        ClassStorage &storage,
        mdTypeDef classToken) noexcept;

    HRESULT InitializeTypeArgs(
        const Profiler &profiler,
        ClassStorage &storage,
        const ProfilerInfo &info,
        ULONG32 typeArgsSize) noexcept;

    HRESULT InitializeSignature(
        const Profiler &profiler,
        IMetaDataImport *pMDImport,
        PCCOR_SIGNATURE &sigBlob,
        ULONG &sigBlobSize) noexcept;

public:
    HRESULT Initialize(
        const Profiler &profiler,
        ClassStorage   &storage) noexcept;
};

#endif // _FUNCTION_INFO_
