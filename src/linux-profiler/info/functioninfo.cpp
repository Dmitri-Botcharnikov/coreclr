#include <utility>
#include <vector>
#include <exception>
#include <stdexcept>

#include <corhlpr.h>

#include "profiler.h"
#include "classstorage.h"
#include "classinfo.h"
#include "default_delete.h"
#include "functioninfo.h"

static __forceinline ULONG CorSigUncompressDataWrapper(
    PCCOR_SIGNATURE &sigBlob, ULONG &sigBlobSize)
{
    HRESULT hr;
    ULONG dataOut;
    ULONG dataLen;

    hr = CorSigUncompressData(sigBlob, sigBlobSize, &dataOut, &dataLen);
    if (FAILED(hr))
    {
        throw HresultException("CorSigUncompressData()", hr);
    }
    sigBlob += dataLen;
    sigBlobSize -= dataLen;

    return dataOut;
}

static __forceinline mdToken CorSigUncompressTokenWrapper(
    PCCOR_SIGNATURE &sigBlob, ULONG &sigBlobSize)
{
    HRESULT hr;
    mdToken token;
    ULONG tokenLen;

    hr = CorSigUncompressToken(sigBlob, sigBlobSize, &token, &tokenLen);
    if (FAILED(hr))
    {
        throw HresultException("CorSigUncompressToken()", hr);
    }
    sigBlob += tokenLen;
    sigBlobSize -= tokenLen;

    return token;
}

static __forceinline ULONG CorSigUncompressCallingConvWrapper(
    PCCOR_SIGNATURE &sigBlob, ULONG &sigBlobSize)
{
    HRESULT hr;
    ULONG callConv;

    hr = CorSigUncompressCallingConv(sigBlob, sigBlobSize, &callConv);
    if (FAILED(hr))
    {
        throw HresultException("CorSigUncompressCallingConv()", hr);
    }
    sigBlob++;
    sigBlobSize--;

    return callConv;
}

static __forceinline CorElementType CorSigUncompressElementTypeWrapper(
    PCCOR_SIGNATURE &sigBlob, ULONG &sigBlobSize)
{
    if (sigBlobSize > 0)
    {
        sigBlobSize--;
        return CorSigUncompressElementType(sigBlob);
    }
    else
    {
        throw HresultException(
            "CorSigUncompressElementType()", META_E_BAD_SIGNATURE);
    }
}

// static
void FunctionInfo::ParseElementType(
    const Profiler &profiler,
    IMetaDataImport *pMDImport,
    PCCOR_SIGNATURE &sigBlob,
    ULONG &sigBlobSize,
    String &str,
    String &arrayBrackets)
{
    bool reiterate;
    ULONG n;
    bool methodFormalArg;
    String appendix;

    do {
        reiterate = false;
        switch (CorSigUncompressElementTypeWrapper(sigBlob, sigBlobSize))
        {
        case ELEMENT_TYPE_VOID:
            str.append(W("void"));
            break;

        case ELEMENT_TYPE_BOOLEAN:
            str.append(W("bool"));
            break;

        case ELEMENT_TYPE_CHAR:
            str.append(W("char"));
            break;

        case ELEMENT_TYPE_I1:
            str.append(W("sbyte"));
            break;

        case ELEMENT_TYPE_U1:
            str.append(W("byte"));
            break;

        case ELEMENT_TYPE_I2:
            str.append(W("short"));
            break;

        case ELEMENT_TYPE_U2:
            str.append(W("ushort"));
            break;

        case ELEMENT_TYPE_I4:
            str.append(W("int"));
            break;

        case ELEMENT_TYPE_U4:
            str.append(W("uint"));
            break;

        case ELEMENT_TYPE_I8:
            str.append(W("long"));
            break;

        case ELEMENT_TYPE_U8:
            str.append(W("ulong"));
            break;

        case ELEMENT_TYPE_R4:
            str.append(W("float"));
            break;

        case ELEMENT_TYPE_R8:
            str.append(W("double"));
            break;

        case ELEMENT_TYPE_STRING:
            str.append(W("string"));
            break;

        case ELEMENT_TYPE_TYPEDBYREF:
            str.append(W("System.TypedReference"));
            break;

        case ELEMENT_TYPE_I:
            str.append(W("System.IntPtr"));
            break;

        case ELEMENT_TYPE_U:
            str.append(W("System.UIntPtr"));
            break;

        case ELEMENT_TYPE_OBJECT:
            str.append(W("object"));
            break;

        case ELEMENT_TYPE_VALUETYPE:
        case ELEMENT_TYPE_CLASS:
            {
                mdToken token = CorSigUncompressTokenWrapper(
                    sigBlob, sigBlobSize);
                String tmp;
                ClassInfo::GetClassNameFromMetaData(
                    profiler, pMDImport, token, tmp, nullptr);
                str.append(tmp);
            }
            break;

        case ELEMENT_TYPE_VAR:
            methodFormalArg = false;
            goto TYPE_ARG;

        case ELEMENT_TYPE_MVAR:
            methodFormalArg = true;
            goto TYPE_ARG;

        TYPE_ARG:
            n = CorSigUncompressDataWrapper(sigBlob, sigBlobSize);
            str.append(ClassInfo::TypeArgName(n, methodFormalArg));
            break;

        case ELEMENT_TYPE_GENERICINST:
            {
                String arrayBrackets;
                FunctionInfo::ParseElementType(
                    profiler, pMDImport, sigBlob, sigBlobSize,
                    str, arrayBrackets);
                if (!arrayBrackets.empty())
                {
                    throw std::logic_error(
                        "FunctionInfo::ParseElementType(): "
                        "Parsing error: Can't parse generic instantiation: "
                        "Instantiation of array class"
                    );
                }
                str.append(1, '<');
                n = CorSigUncompressDataWrapper(sigBlob, sigBlobSize);
                for (ULONG i = 0; i < n; i++)
                {
                    if (i != 0)
                    {
                        str.append(W(", "));
                    }
                    String arrayBrackets;
                    FunctionInfo::ParseElementType(
                        profiler, pMDImport, sigBlob, sigBlobSize,
                        str, arrayBrackets);
                    str.append(arrayBrackets);
                }
                str.append(1, '>');
            }
            break;

        case ELEMENT_TYPE_FNPTR:
            {
                String returnType;
                String signature;
                FunctionInfo::ParseSignature(
                    profiler, pMDImport, sigBlob, sigBlobSize,
                    returnType, signature);
                str.append(returnType).append(W(" *")).append(signature);
            }
            break;

        case ELEMENT_TYPE_ARRAY:
            {
                FunctionInfo::ParseElementType(
                    profiler, pMDImport, sigBlob, sigBlobSize,
                    str, arrayBrackets);
                ULONG rank = CorSigUncompressDataWrapper(sigBlob, sigBlobSize);
                if (rank == 0)
                {
                    throw std::logic_error(
                        "FunctionInfo::ParseElementType(): "
                        "Parsing error: Can't parse array class: "
                        "Zero rank of array class"
                    );
                }

                // Skip array sizes.
                n = CorSigUncompressDataWrapper(sigBlob, sigBlobSize);
                if (n > rank)
                {
                    throw std::logic_error(
                        "FunctionInfo::ParseElementType(): "
                        "Parsing error: Can't parse array class: "
                        "Too many sizes"
                    );
                }
                for(ULONG i = 0; i < n; i++)
                {
                    CorSigUncompressDataWrapper(sigBlob, sigBlobSize);
                }

                // Skip array lower bounds.
                n = CorSigUncompressDataWrapper(sigBlob, sigBlobSize);
                if (n > rank)
                {
                    throw std::logic_error(
                        "FunctionInfo::ParseElementType(): "
                        "Parsing error: Can't parse array class: "
                        "Too many lower bounds"
                    );
                }
                for(ULONG i = 0; i < n; i++)
                {
                    CorSigUncompressDataWrapper(sigBlob, sigBlobSize);
                }

                arrayBrackets.insert(
                    0, String(1, W('['))
                        .append(rank - 1, W(','))
                        .append(1, W(']'))
                );
            }
            break;

        case ELEMENT_TYPE_SZARRAY:
            FunctionInfo::ParseElementType(
                profiler, pMDImport, sigBlob, sigBlobSize,
                str, arrayBrackets);
            arrayBrackets.insert(0, W("[]"));
            break;

        case ELEMENT_TYPE_BYREF:
            str.append(W("ref "));
            goto REITERATE;

        case ELEMENT_TYPE_PTR:
            appendix.insert(0, W("*"));
            goto REITERATE;

        case ELEMENT_TYPE_CMOD_OPT:
        case ELEMENT_TYPE_CMOD_REQD:
            // Skip class token.
            CorSigUncompressTokenWrapper(sigBlob, sigBlobSize);
        case ELEMENT_TYPE_PINNED:
            goto REITERATE;

        default:
            throw std::logic_error(
                "FunctionInfo::ParseElementType(): "
                "Parsing error: Can't parse unknown element type"
            );
            break;

        REITERATE:
            reiterate = true;
            break;
        }
    } while(reiterate);
    str.append(appendix);
}

// static
void FunctionInfo::ParseSignature(
    const Profiler &profiler,
    IMetaDataImport *pMDImport,
    PCCOR_SIGNATURE &sigBlob,
    ULONG &sigBlobSize,
    String &returnType,
    String &signature)
{
    ULONG argNum = 0;
    ULONG argCount = 0;
    bool argCountDetermined = false;
    bool openBracketAppended = false;
    bool closeBracketAppended = false;

    try
    {
        // Get the calling convention out.
        ULONG callConv = CorSigUncompressCallingConvWrapper(
            sigBlob, sigBlobSize);

        // Should not be a local variable, field or generic instantiation
        // signature.
        if (
            isCallConv(callConv, IMAGE_CEE_CS_CALLCONV_LOCAL_SIG) ||
            isCallConv(callConv, IMAGE_CEE_CS_CALLCONV_FIELD) ||
            isCallConv(callConv, IMAGE_CEE_CS_CALLCONV_GENERICINST)
        )
        {
            throw std::logic_error(
                "FunctionInfo::ParseSignature(): "
                "Parsing error: Can't parse signature: "
                "Unexpected calling convention"
            );
        }

        // Skip the number of method type arguments for generic methods.
        if (callConv & IMAGE_CEE_CS_CALLCONV_GENERIC)
        {
            CorSigUncompressDataWrapper(sigBlob, sigBlobSize);
        }

        // Get the number of arguments.
        argCount = CorSigUncompressDataWrapper(sigBlob, sigBlobSize);
        argCountDetermined = true;

        // Get the return type.
        String type;
        String arrayBrackets;
        FunctionInfo::ParseElementType(
            profiler, pMDImport, sigBlob, sigBlobSize, type, arrayBrackets);
        returnType.append(type + arrayBrackets);

        // Calculate signature.
        signature.append(1, W('('));
        openBracketAppended = true;
        while(argNum < argCount)
        {
            String delimeter = argNum != 0 ? W(", ") : W("");

            if (sigBlobSize == 0)
            {
                throw HresultException(
                    "CorSigUncompressElementType()", META_E_BAD_SIGNATURE);
            }
            else if (*sigBlob == ELEMENT_TYPE_SENTINEL)
            {
                CorSigUncompressElementTypeWrapper(sigBlob, sigBlobSize);
                signature.append(delimeter + W("..."));
            }
            else
            {
                String type;
                String arrayBrackets;
                FunctionInfo::ParseElementType(
                    profiler, pMDImport, sigBlob, sigBlobSize,
                    type, arrayBrackets);
                signature.append(delimeter + type + arrayBrackets);
                argNum++;
            }
        }
        signature.append(1, W(')'));
        closeBracketAppended = true;
    }
    catch (const std::exception &e)
    {
        if (returnType.empty())
        {
            returnType = W("<UNKNOWN>");
        }

        try
        {
            if (!openBracketAppended)
            {
                signature.append(1, W('('));
            }
            if (argCountDetermined)
            {
                for(; argNum < argCount; argNum++)
                {
                    if (argNum != 0)
                    {
                        signature.append(W(", <UNKNOWN>"));
                    }
                    else
                    {
                        signature.append(W("<UNKNOWN>"));
                    }
                }
            }
            else
            {
                signature.append(W("?"));
            }
            if (!closeBracketAppended)
            {
                signature.append(1, W(')'));
            }
        }
        catch (const std::exception &e)
        {
            signature = W("(?)");
        }

        throw;
    }
}

__forceinline HRESULT FunctionInfo::InitializeCodeInfo(
    const Profiler &profiler,
    const ProfilerInfo &info) noexcept
{
    HRESULT hr = S_OK;

    try
    {
        _ASSERTE(info.version() >= 2);

        ULONG32 size;
        hr = info.v2()->GetCodeInfo2(this->id, 0, &size, nullptr);
        if (SUCCEEDED(hr) && size > 0)
        {
            this->codeInfo.resize(size);
            // codeInfo.data() can be used safety now.
            hr = info.v2()->GetCodeInfo2(
                this->id, size, nullptr, this->codeInfo.data());
        }

        if (FAILED(hr))
        {
            throw HresultException(
                "FunctionInfo::InitializeCodeInfo(): GetCodeInfo2()", hr);
        }
    }
    catch (const std::exception &e)
    {
        this->codeInfo.clear();
        this->codeInfo.shrink_to_fit();
        hr = profiler.HandleException(e);
    }

    return hr;
}

__forceinline HRESULT FunctionInfo::InitializeILToNativeMapping(
    const Profiler &profiler,
    const ProfilerInfo &info) noexcept
{
    HRESULT hr;

    try
    {
        ULONG32 size;
        hr = info.v1()->GetILToNativeMapping(this->id, 0, &size, nullptr);
        if (SUCCEEDED(hr) && size > 0)
        {
            this->ILToNativeMapping.resize(size);
            // ILToNativeMapping.data() can be used safety now.
            hr = info.v1()->GetILToNativeMapping(
                this->id, size, &size, this->ILToNativeMapping.data());
        }
        if (FAILED(hr))
        {
            throw HresultException(
                "FunctionInfo::InitializeILToNativeMapping(): "
                "GetILToNativeMapping()", hr
            );
        }
        else if (this->ILToNativeMapping.size() != size)
        {
            throw std::logic_error(
                "FunctionInfo::InitializeILToNativeMapping(): "
                "GetILToNativeMapping(): Unexpected map size"
            );
        }
    }
    catch (const std::exception &e)
    {
        this->ILToNativeMapping.clear();
        this->ILToNativeMapping.shrink_to_fit();
        hr = profiler.HandleException(e);
    }

    return hr;
}

__forceinline HRESULT FunctionInfo::InitializeFunctionName(
    const Profiler &profiler,
    IMetaDataImport *pMDImport,
    ULONG funcNameSize) noexcept
{
    HRESULT hr = S_OK;

    try
    {
        std::vector<WCHAR> funcNameBuffer(funcNameSize);
        // funcNameBuffer.data() can be used safety now.
        hr = pMDImport->GetMethodProps(
            /* [in]  method token   */ this->funcToken,
            /* [out] class token    */ nullptr,
            /* [out] name buffer    */ funcNameBuffer.data(),
            /* [in]  buffer size    */ funcNameSize,
            /* [out] name length    */ nullptr,
            /* [out] method flags   */ nullptr,
            /* [out] signature blob */ nullptr,
            /* [out] size of blob   */ nullptr,
            /* [out] RVA pointer    */ nullptr,
            /* [out] impl. flags    */ nullptr
        );
        if (FAILED(hr))
        {
            throw HresultException(
                "FunctionInfo::InitializeFunctionName(): "
                "GetMethodProps()", hr
            );
        }
        this->name = funcNameBuffer.data();
    }
    catch (const std::exception &e)
    {
        this->name = W("<UNKNOWN>");
        hr = profiler.HandleException(e);
    }

    return hr;
}

__forceinline HRESULT FunctionInfo::InitializeOwnerClassFromClassId(
    const Profiler &profiler,
    ClassStorage &storage) noexcept
{
    HRESULT hr = S_OK;

    try
    {
        this->ownerClass = &storage.Place(this->classId).first;
        hr = this->ownerClass->Initialize(profiler, storage);
    }
    catch (const std::exception &e)
    {
        hr = profiler.HandleException(e);
    }

    return hr;
}

__forceinline HRESULT FunctionInfo::InitializeOwnerClassFromClassToken(
    const Profiler &profiler,
    ClassStorage &storage,
    mdTypeDef classToken) noexcept
{
    HRESULT hr = S_OK;

    try
    {
        this->ownerClass = &storage.Add();
        hr = this->ownerClass->InitializeFromToken(
            profiler, this->moduleId, classToken);
    }
    catch (const std::exception &e)
    {
        hr = profiler.HandleException(e);
    }

    return hr;
}

__forceinline HRESULT FunctionInfo::InitializeTypeArgs(
    const Profiler &profiler,
    ClassStorage &storage,
    const ProfilerInfo &info,
    ULONG32 typeArgsSize) noexcept
{
    HRESULT hrReturn = S_OK;
    HRESULT hr;

    try
    {
        if (typeArgsSize > 0)
        {
            this->typeArgs.resize(typeArgsSize);
            std::vector<ClassID> typeArgIds(typeArgsSize);
            // typeArgIds.data() can be used safety now.
            hr = info.v2()->GetFunctionInfo2(
                /* [in]  function ID           */ this->id,
                /* [in]  frame info            */ 0,
                /* [in]  class ID              */ nullptr,
                /* [out] module ID             */ nullptr,
                /* [out] function token        */ nullptr,
                /* [in]  type args buffer size */ typeArgsSize,
                /* [out] number of type args   */ nullptr,
                /* [out] type args buffer      */ typeArgIds.data()
            );
            if (FAILED(hr))
            {
                throw HresultException(
                    "FunctionInfo::InitializeTypeArgs(): "
                    "GetFunctionInfo2()", hr
                );
            }

            for (ULONG32 i = 0; i < typeArgsSize; i++)
            {
                try
                {
                    if (typeArgIds[i] != 0)
                    {
                        this->typeArgs[i] = &storage.Place(typeArgIds[i]).first;
                        hr = this->typeArgs[i]->Initialize(profiler, storage);
                    }
                }
                catch (const std::exception &e)
                {
                    hr = profiler.HandleException(e);
                }
                if (FAILED(hr) && SUCCEEDED(hrReturn))
                {
                    hrReturn = hr;
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        hr = profiler.HandleException(e);
        if (FAILED(hr) && SUCCEEDED(hrReturn))
        {
            hrReturn = hr;
        }
    }

    return hrReturn;
}

__forceinline HRESULT FunctionInfo::InitializeSignature(
    const Profiler &profiler,
    IMetaDataImport *pMDImport,
    PCCOR_SIGNATURE &sigBlob,
    ULONG &sigBlobSize) noexcept
{
    HRESULT hr = S_OK;

    try
    {
        FunctionInfo::ParseSignature(
            profiler, pMDImport, sigBlob, sigBlobSize,
            this->returnType, this->signature);
        if (sigBlobSize != 0)
        {
            profiler.LOG().Warn() <<
                "FunctionInfo::InitializeSignature(): Ambiguous signature blob";
        }
    }
    catch (const std::exception &e)
    {
        hr = profiler.HandleException(e);
    }

    return hr;
}

HRESULT FunctionInfo::Initialize(
    const Profiler &profiler,
    ClassStorage &storage) noexcept
{
    HRESULT hrReturn = S_OK;
    HRESULT hr;

    if (this->isInitialized)
    {
        return hrReturn;
    }

    _ASSERTE(this->id != 0);
    const ProfilerInfo &info = profiler.GetProfilerInfo();

    hr = FunctionInfo::InitializeCodeInfo(profiler, info);
    if (FAILED(hr) && SUCCEEDED(hrReturn))
    {
        hrReturn = hr;
    }

    hr = FunctionInfo::InitializeILToNativeMapping(profiler, info);
    if (FAILED(hr) && SUCCEEDED(hrReturn))
    {
        hrReturn = hr;
    }

    try
    {
        //
        // Get Common Info.
        //

        ULONG32 typeArgsSize;
        _ASSERTE(info.version() >= 2);
        hr = info.v2()->GetFunctionInfo2(
            /* [in]  function ID           */ this->id,
            /* [in]  frame info            */ 0,
            /* [in]  class ID              */ &this->classId,
            /* [out] module ID             */ &this->moduleId,
            /* [out] function token        */ &this->funcToken,
            /* [in]  type args buffer size */ 0,
            /* [out] number of type args   */ &typeArgsSize,
            /* [out] type args buffer      */ nullptr
        );
        if (FAILED(hr))
        {
            throw HresultException(
                "FunctionInfo::Initialize(): GetFunctionInfo2()", hr);
        }
        else if (TypeFromToken(this->funcToken) != mdtMethodDef)
        {
            throw std::logic_error(
                "FunctionInfo::Initialize(): "
                "GetTokenAndMetaDataFromFunction(): Unexpected method token"
            );
        }

        IUnknown *pUnknown;
        hr = info.v1()->GetModuleMetaData(
            this->moduleId, ofRead, IID_IMetaDataImport, &pUnknown);
        if (FAILED(hr))
        {
            throw HresultException(
                "FunctionInfo::Initialize(): GetModuleMetaData()", hr);
        }
        std::unique_ptr<IUnknown> pUnknownHolder(pUnknown);
        IMetaDataImport *pMDImport = dynamic_cast<IMetaDataImport*>(pUnknown);

        ULONG funcNameSize;
        mdTypeDef classToken;
        DWORD funcAttr;
        PCCOR_SIGNATURE sigBlob;
        ULONG sigBlobSize;
        hr = pMDImport->GetMethodProps(
            /* [in]  method token   */ this->funcToken,
            /* [out] class token    */ &classToken,
            /* [out] name buffer    */ nullptr,
            /* [in]  buffer size    */ 0,
            /* [out] name length    */ &funcNameSize,
            /* [out] method flags   */ &funcAttr,
            /* [out] signature blob */ &sigBlob,
            /* [out] size of blob   */ &sigBlobSize,
            /* [out] RVA pointer    */ nullptr,
            /* [out] impl. flags    */ nullptr
        );
        if (FAILED(hr))
        {
            throw HresultException(
                "FunctionInfo::Initialize(): GetMethodProps()", hr);
        }

        //
        // Get Function Name.
        //

        hr = this->InitializeFunctionName(profiler, pMDImport, funcNameSize);
        if (FAILED(hr) && SUCCEEDED(hrReturn))
        {
            hrReturn = hr;
        }

        //
        // Get Owner Class.
        //

        if (this->classId != 0)
        {
            hr = this->InitializeOwnerClassFromClassId(profiler, storage);
        }
        else
        {
            hr = this->InitializeOwnerClassFromClassToken(
                profiler, storage, classToken);
        }
        if (FAILED(hr) && SUCCEEDED(hrReturn))
        {
            hrReturn = hr;
        }

        //
        // Get Type Arguments.
        //

        hr = this->InitializeTypeArgs(
            profiler, storage, info, typeArgsSize);
        if (FAILED(hr) && SUCCEEDED(hrReturn))
        {
            hrReturn = hr;
        }

        //
        // Get Return Type and Signature.
        //

        hr = FunctionInfo::InitializeSignature(
            profiler, pMDImport, sigBlob, sigBlobSize);
        if (FAILED(hr) && SUCCEEDED(hrReturn))
        {
            hrReturn = hr;
        }
    }
    catch (const std::exception &e)
    {
        this->name       = W("<UNKNOWN>");
        this->returnType = W("<UNKNOWN>");
        this->signature  = W("(?)");
        hr = profiler.HandleException(e);
        if (FAILED(hr) && SUCCEEDED(hrReturn))
        {
            hrReturn = hr;
        }
    }

    try
    {
        if (this->ownerClass)
        {
            this->fullName = this->ownerClass->fullName;
        }
        else
        {
            this->fullName = W("<UNKNOWN>");
        }
        this->fullName.append(W("::")).append(this->name);
        if (!this->typeArgs.empty())
        {
            ClassInfo::AppendTypeArgNames(
                this->fullName, this->typeArgs, true);
        }
    }
    catch (const std::exception &e)
    {
        this->fullName = W("<UNKNOWN>");
        hr = profiler.HandleException(e);
        if (FAILED(hr) && SUCCEEDED(hrReturn))
        {
            hrReturn = hr;
        }
    }

    this->isInitialized = true;
    return hr;
}
