#ifndef _CLASS_INFO_
#define _CLASS_INFO_

#include <vector>
#include <string>

#include <cor.h>
#include <corhdr.h>
#include <corprof.h>

#include "mappedinfo.h"

class Profiler;

class ProfilerInfo;

class ClassStorage;

struct ClassInfo : public MappedInfo<ClassID>
{
    typedef std::basic_string<WCHAR> String;

    ModuleID  moduleId;
    mdTypeDef classToken = mdTypeDefNil;
    std::vector<ClassInfo*> typeArgs;
    ULONG     rank;
    String    name;
    String    arrayBrackets;
    String    fullName;
    bool      isInitialized;
    bool      isNamePrinted;

public:
    static HRESULT GetClassNameFromMetaData(
        const Profiler &profiler,
        IMetaDataImport *pMDImport,
        mdToken classToken,
        String &className,
        ULONG32 *typeArgsCount) noexcept;

    static String TypeArgName(
        ULONG argIndex,
        bool methodFormalArg);

    static void AppendTypeArgNames(
        String &str,
        const std::vector<ClassInfo*> &typeArgs,
        bool methodFormalArg);

private:
    static String GetNameFromElementType(
        CorElementType elementType);

    HRESULT InitializeArrayClass(
        const Profiler &profiler,
        ClassStorage &storage,
        ClassID realClassID,
        CorElementType elementType) noexcept;

    HRESULT InitializeRegularClassName(
        const Profiler &profiler,
        const ProfilerInfo &info) noexcept;

    HRESULT InitializeTypeArgs(
        const Profiler &profiler,
        ClassStorage &storage,
        const ProfilerInfo &info,
        ULONG32 typeArgsSize) noexcept;

public:
    HRESULT Initialize(
        const Profiler &profiler,
        ClassStorage &storage) noexcept;

    HRESULT InitializeFromToken(
        const Profiler &profiler,
        ModuleID moduleId,
        mdTypeDef classToken) noexcept;
};

#endif // _CLASS_INFO_
