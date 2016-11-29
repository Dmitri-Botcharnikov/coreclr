#include <utility>
#include <array>
#include <vector>
#include <exception>
#include <stdexcept>

#include "profiler.h"
#include "classstorage.h"
#include "default_delete.h"
#include "classinfo.h"

// static
HRESULT ClassInfo::GetClassNameFromMetaData(
    const Profiler &profiler,
    IMetaDataImport *pMDImport,
    mdToken classToken,
    String &className,
    ULONG32 *typeArgsCount) noexcept
{
    HRESULT hr = S_OK;

    if (IsNilToken(classToken))
    {
        className =  W("<UNKNOWN>");
        return hr;
    }

    try
    {
        std::vector<WCHAR> classNameBuffer;
        ULONG classNameSize;
        if (TypeFromToken(classToken) == mdtTypeDef)
        {
            DWORD dwTypeDefFlags;

            hr = pMDImport->GetTypeDefProps(
                /* [in]  type token  */ classToken,
                /* [out] name buffer */ nullptr,
                /* [in]  buffer size */ 0,
                /* [out] name length */ &classNameSize,
                /* [out] type flags  */ &dwTypeDefFlags,
                /* [out] base type   */ nullptr
            );
            if (SUCCEEDED(hr))
            {
                classNameBuffer.resize(classNameSize);
                // classNameBuffer.data() can be used safety now.
                hr = pMDImport->GetTypeDefProps(
                    /* [in]  type token  */ classToken,
                    /* [out] name buffer */ classNameBuffer.data(),
                    /* [in]  buffer size */ classNameSize,
                    /* [out] name length */ &classNameSize,
                    /* [out] type flags  */ nullptr,
                    /* [out] base type   */ nullptr
                );
            }
            if (FAILED(hr))
            {
                throw HresultException(
                    "ClassInfo::GetClassNameFromMetaData(): "
                    "GetTypeDefProps()", hr
                );
            }

            if (IsTdNested(dwTypeDefFlags))
            {
                mdTypeDef enclosingClass;
                hr = pMDImport->GetNestedClassProps(classToken, &enclosingClass);
                if (SUCCEEDED(hr))
                {
                    hr = ClassInfo::GetClassNameFromMetaData(
                        profiler, pMDImport, enclosingClass, className,
                        typeArgsCount);
                }
                else
                {
                    throw HresultException(
                        "ClassInfo::GetClassNameFromMetaData(): "
                        "GetNestedClassProps()", hr
                    );
                }
                className.append(1, '.').append(classNameBuffer.data());
            }
            else
            {
                className = classNameBuffer.data();
            }
        }
        else if (TypeFromToken(classToken) == mdtTypeRef)
        {
            mdToken scopeToken;

            hr = pMDImport->GetTypeRefProps(
                /* [in]  type token  */ classToken,
                /* [out] scope token */ &scopeToken,
                /* [out] name buffer */ nullptr,
                /* [in]  buffer size */ 0,
                /* [out] name length */ &classNameSize
            );
            if (SUCCEEDED(hr))
            {
                classNameBuffer.resize(classNameSize);
                // classNameBuffer.data() can be used safety now.
                hr = pMDImport->GetTypeRefProps(
                    /* [in]  type token  */ classToken,
                    /* [out] scope token */ nullptr,
                    /* [out] name buffer */ classNameBuffer.data(),
                    /* [in]  buffer size */ classNameSize,
                    /* [out] name length */ &classNameSize
                );
            }
            if (FAILED(hr))
            {
                throw HresultException(
                    "ClassInfo::GetClassNameFromMetaData(): "
                    "GetTypeRefProps()", hr
                );
            }

            if (TypeFromToken(scopeToken) == mdtTypeRef)
            {
                hr = ClassInfo::GetClassNameFromMetaData(
                    profiler, pMDImport, scopeToken, className, typeArgsCount);
                className.append(1, '.').append(classNameBuffer.data());
            }
            else
            {
                className = classNameBuffer.data();
            }
        }
        else
        {
            throw std::logic_error(
                "ClassInfo::GetClassNameFromMetaData(): Unexpected token type");
        }

        String::size_type pos = className.find_last_of('`');
        if (pos != String::npos)
        {
            if (typeArgsCount)
            {
                ULONG32 count = PAL_wcstoul(
                    className.data() + pos + 1, nullptr, 10);
                *typeArgsCount += count;
            }
            className.erase(pos);
        }
        if (className.empty())
        {
            className = W("<EMPTY>");
        }
    }
    catch (const std::exception &e)
    {
        className = W("<UNKNOWN>");
        hr = profiler.HandleException(e);
    }

    return hr;
}

// static
ClassInfo::String ClassInfo::TypeArgName(
    ULONG argIndex,
    bool methodFormalArg)
{
    char argStart = methodFormalArg ? 'M' : 'T';
    if (argIndex <= 6)
    {
        // The first 7 parameters are printed as M, N, O, P, Q, R, S
        // or as T, U, V, W, X, Y, Z.
        return String(1, argStart + argIndex);
    }
    else
    {
        // Everything after that as M7, M8, ... or T7, T8, ...
        std::array<WCHAR, 4> argName;
        _snwprintf(
            argName.data(), argName.size(), W("%c%u"), argStart, argIndex);
        return argName.data();
    }
}

// static
void ClassInfo::AppendTypeArgNames(
    String &str,
    const std::vector<ClassInfo*> &typeArgs,
    bool methodFormalArg)
{
    _ASSERTE(!typeArgs.empty());

    str.append(1, W('<'));
    for (ULONG i = 0; i < typeArgs.size(); i++)
    {
        if (i != 0)
        {
            str.append(W(", "));
        }
        str.append(ClassInfo::TypeArgName(i, methodFormalArg));
        if (typeArgs[i])
        {
            str.append(W("=")).append(typeArgs[i]->name);
        }
    }
    str.append(1, W('>'));
}

// static
__forceinline ClassInfo::String ClassInfo::GetNameFromElementType(
    CorElementType elementType) noexcept
{
    switch (elementType)
    {
    case ELEMENT_TYPE_VOID:
        return W("System.Void");

    case ELEMENT_TYPE_BOOLEAN:
        return W("System.Boolean");

    case ELEMENT_TYPE_CHAR:
        return W("System.Char");

    case ELEMENT_TYPE_I1:
        return W("System.SByte");

    case ELEMENT_TYPE_U1:
        return W("System.Byte");

    case ELEMENT_TYPE_I2:
        return W("System.Int16");

    case ELEMENT_TYPE_U2:
        return W("System.UInt16");

    case ELEMENT_TYPE_I4:
        return W("System.Int32");

    case ELEMENT_TYPE_U4:
        return W("System.UInt32");

    case ELEMENT_TYPE_I8:
        return W("System.Int64");

    case ELEMENT_TYPE_U8:
        return W("System.UInt64");

    case ELEMENT_TYPE_R4:
        return W("System.Single");

    case ELEMENT_TYPE_R8:
        return W("System.Double");

    case ELEMENT_TYPE_STRING:
        return W("System.String");

    case ELEMENT_TYPE_PTR:
        return W("<UNKNOWN>*");

    case ELEMENT_TYPE_BYREF:
        return W("ref <UNKNOWN>");

    case ELEMENT_TYPE_VALUETYPE:
        return W("System.ValueType");

    case ELEMENT_TYPE_CLASS:
    case ELEMENT_TYPE_OBJECT:
        return W("System.Object");

    case ELEMENT_TYPE_ARRAY:
    case ELEMENT_TYPE_SZARRAY:
        return W("System.Array");

    case ELEMENT_TYPE_TYPEDBYREF:
        return W("System.TypedReference");

    case ELEMENT_TYPE_I:
        return W("System.IntPtr");

    case ELEMENT_TYPE_U:
        return W("System.UIntPtr");

    default:
        return W("<UNKNOWN>");
    }
}

__forceinline HRESULT ClassInfo::InitializeArrayClass(
    const Profiler &profiler,
    ClassStorage &storage,
    ClassID realClassID,
    CorElementType elementType) noexcept
{
    HRESULT hr = S_OK;

    try
    {
        if (realClassID != 0 &&
            (
                elementType != ELEMENT_TYPE_PTR   &&
                elementType != ELEMENT_TYPE_BYREF &&
                elementType != ELEMENT_TYPE_FNPTR
            )
        )
        {
            ClassInfo &realClass = storage.Place(realClassID).first;
            hr = realClass.Initialize(profiler, storage);
            // Class name of array class can contains type arguments
            // of real class. Array brackets is calculated separately.
            if (realClass.arrayBrackets.empty())
            {
                this->name = realClass.fullName;
            }
            else
            {
                this->name = realClass.name;
            }
            this->arrayBrackets = realClass.arrayBrackets.c_str();
        }
        else
        {
            this->name = ClassInfo::GetNameFromElementType(elementType);
        }

        _ASSERTE(this->rank >= 1);
        this->arrayBrackets.insert(
            0, String(1, W('['))
                .append(this->rank - 1, W(','))
                .append(1, W(']'))
        );
    }
    catch (const std::exception &e)
    {
        this->name = W("<UNKNOWN>");
        this->arrayBrackets = W("[?]");
        hr = profiler.HandleException(e);
    }

    return hr;
}

__forceinline HRESULT ClassInfo::InitializeRegularClassName(
    const Profiler &profiler,
    const ProfilerInfo &info) noexcept
{
    HRESULT hr = S_OK;

    try
    {
        IUnknown *pUnknown;
        hr = info.v1()->GetModuleMetaData(
            this->moduleId, ofRead, IID_IMetaDataImport, &pUnknown);
        if (FAILED(hr))
        {
            throw HresultException(
                "ClassInfo::InitializeRegularClassName(): "
                "GetModuleMetaData()", hr
            );
        }
        std::unique_ptr<IUnknown> pUnknownHolder(pUnknown);
        IMetaDataImport *pMDImport = dynamic_cast<IMetaDataImport*>(pUnknown);

        ULONG32 typeArgsSize = 0;
        hr = ClassInfo::GetClassNameFromMetaData(
            profiler, pMDImport, this->classToken, this->name, &typeArgsSize);
        if (this->typeArgs.empty())
        {
            this->typeArgs.resize(typeArgsSize);
        }
    }
    catch (const std::exception &e)
    {
        this->name = W("<UNKNOWN>");
        hr = profiler.HandleException(e);
    }

    return hr;
}

__forceinline HRESULT ClassInfo::InitializeTypeArgs(
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
            hr = info.v2()->GetClassIDInfo2(
                /* [in]  class ID              */ this->id,
                /* [out] module ID             */ nullptr,
                /* [out] class token           */ nullptr,
                /* [out] parent class ID       */ nullptr,
                /* [in]  type args buffer size */ typeArgsSize,
                /* [out] number of type args   */ nullptr,
                /* [out] type args buffer      */ typeArgIds.data()
            );
            if (FAILED(hr))
            {
                throw HresultException(
                    "ClassInfo::InitializeTypeArgs(): "
                    "GetClassIDInfo2()", hr
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

HRESULT ClassInfo::Initialize(
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

    try
    {
        //
        // Get Common Info.
        //

        ClassID realClassID;
        CorElementType elementType;
        hr = info.v1()->IsArrayClass(
            this->id, &elementType, &realClassID, &this->rank);
        if (FAILED(hr))
        {
            throw HresultException(
                "ClassInfo::Initialize(): IsArrayClass()", hr);
        }

        if (hr == S_OK)
        {
            //
            // Array class handling.
            //

            if (rank == 0)
            {
                throw std::logic_error(
                    "ClassInfo::Initialize(): IsArrayClass(): "
                    "Zero rank for array class"
                );
            }

            hr = this->InitializeArrayClass(
                profiler, storage, realClassID, elementType);
            if (FAILED(hr) && SUCCEEDED(hrReturn))
            {
                hrReturn = hr;
            }
        }
        else if (hr == S_FALSE)
        {
            //
            // Regular class handling.
            //

            if (rank != 0)
            {
                throw std::logic_error(
                    "ClassInfo::Initialize(): IsArrayClass(): "
                    "Non-zero rank for regular class"
                );
            }

            ULONG32 typeArgsSize;
            _ASSERTE(info.version() >= 2);
            hr = info.v2()->GetClassIDInfo2(
                /* [in]  class ID              */ this->id,
                /* [out] module ID             */ &this->moduleId,
                /* [out] class token           */ &this->classToken,
                /* [out] parent class ID       */ nullptr,
                /* [in]  type args buffer size */ 0,
                /* [out] number of type args   */ &typeArgsSize,
                /* [out] type args buffer      */ nullptr
            );
            if (FAILED(hr))
            {
                throw HresultException(
                    "ClassInfo::Initialize(): GetClassIDInfo2()", hr);
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
            // Get Class Name.
            //

            hr = this->InitializeRegularClassName(profiler, info);
            if (FAILED(hr) && SUCCEEDED(hrReturn))
            {
                hrReturn = hr;
            }
        }
        else
        {
            throw std::logic_error(
                "ClassInfo::Initialize(): IsArrayClass(): Unexpected HRESULT");
        }
    }
    catch (const std::exception &e)
    {
        this->name = W("<UNKNOWN>");
        hr = profiler.HandleException(e);
        if (FAILED(hr) && SUCCEEDED(hrReturn))
        {
            hrReturn = hr;
        }
    }

    try
    {
        this->fullName = this->name;
        _ASSERTE(this->arrayBrackets.empty() || this->typeArgs.empty());
        if (!this->arrayBrackets.empty())
        {
            //
            // Array class handling.
            //

            this->fullName.append(this->arrayBrackets);
        }
        else if (!this->typeArgs.empty())
        {
            //
            // Generic class handling.
            //

            ClassInfo::AppendTypeArgNames(
                this->fullName, this->typeArgs, false);
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
    return hrReturn;
}

HRESULT ClassInfo::InitializeFromToken(
    const Profiler &profiler,
    ModuleID moduleId,
    mdTypeDef classToken) noexcept
{
    HRESULT hrReturn = S_OK;
    HRESULT hr;

    if (this->isInitialized)
    {
        return hrReturn;
    }

    _ASSERTE(this->id == 0);
    _ASSERTE(this->rank == 0);
    _ASSERTE(this->arrayBrackets.empty());
    const ProfilerInfo &info = profiler.GetProfilerInfo();

    try
    {
        this->moduleId = moduleId;
        this->classToken = classToken;

        //
        // Get Class Name.
        //

        hr = this->InitializeRegularClassName(profiler, info);
        if (FAILED(hr) && SUCCEEDED(hrReturn))
        {
            hrReturn = hr;
        }
    }
    catch (const std::exception &e)
    {
        this->name = W("<UNKNOWN>");
        hr = profiler.HandleException(e);
        if (FAILED(hr) && SUCCEEDED(hrReturn))
        {
            hrReturn = hr;
        }
    }

    try
    {
        this->fullName = this->name;
        if (!this->typeArgs.empty())
        {
            ClassInfo::AppendTypeArgNames(
                this->fullName, this->typeArgs, false);
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
    return hrReturn;
}
