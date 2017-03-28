#ifndef _EXECUTION_TRACE_H_
#define _EXECUTION_TRACE_H_

#include <functional>

#include <cor.h>
#include <corhdr.h>
#include <corprof.h>

#include "basetrace.h"

#include "sharedresource.h"
#include "threadinfo.h"
#include "functionstorage.h"

// #include "shared_iterator_range.h"

class ExecutionTrace final : public BaseTrace
{
private:
    using SamplingSharedState = CommonTrace::SamplingSharedState;

public:
    ExecutionTrace(Profiler &profiler);

    ~ExecutionTrace();

    void ProcessConfig(ProfilerConfig &config);

    bool IsPseudoFunction(const FunctionInfo &funcInfo) const noexcept;

    void Shutdown() noexcept;

    UINT_PTR GetCurrentManagedIP(
        ThreadInfo &thrInfo, CONTEXT *winContext = nullptr) noexcept;

    void RestoreManagedIP(
        ThreadInfo &thrInfo, CONTEXT *winContext = nullptr) noexcept;

    bool NeedSample(
        ThreadInfo &thrInfo, SamplingSharedState &state) const noexcept;

    void PrepareSample(
        ThreadInfo &thrInfo, SamplingSharedState &state) noexcept;

    void AfterSample(
        ThreadInfo &thrInfo, SamplingSharedState &state) noexcept;

private:
    //
    // Various useful instance methods.
    //

    void UpdateCallStackPush(const FunctionInfo &funcInfo) noexcept;

    void UpdateCallStackPush(
        const FunctionInfo &funcInfo, UINT_PTR prevIP) noexcept;

    void UpdateCallStackPop() noexcept;

public:
    //
    // Function Hooks and mapper function.
    // Used by stub function so have to be public.
    //

    UINT_PTR FunctionIDMap(
        FunctionID funcId,
        BOOL *pbHookFunction) noexcept;

    void Enter(const FunctionInfo &funcInfo, UINT_PTR prevIP) noexcept;

    void Leave(const FunctionInfo &funcInfo) noexcept;

    void Tailcall(const FunctionInfo &funcInfo) noexcept;

private:
    //
    // Events helpers.
    //

    typedef void JITDumpFunction(const FunctionInfo &funcInfo);

    HRESULT JITStarted(
        FunctionID functionId) noexcept;

    HRESULT JITFinished(
        FunctionID functionId,
        std::function<JITDumpFunction> dumpFunction) noexcept;
public:
    //
    // Events.
    //

    HRESULT FunctionUnloadStarted(
        FunctionID functionId) noexcept;

    HRESULT JITCompilationStarted(
        FunctionID functionId,
        BOOL fIsSafeToBlock) noexcept;

    HRESULT JITCompilationFinished(
        FunctionID functionId,
        HRESULT hrStatus, BOOL fIsSafeToBlock) noexcept;

    HRESULT JITCachedFunctionSearchStarted(
        FunctionID functionId,
        BOOL *pbUseCachedFunction) noexcept;

    HRESULT JITCachedFunctionSearchFinished(
        FunctionID functionId,
        COR_PRF_JIT_CACHE result) noexcept;

    HRESULT UnmanagedToManagedTransition(
        FunctionID functionId,
        COR_PRF_TRANSITION_REASON reason) noexcept;

    HRESULT ManagedToUnmanagedTransition(
        FunctionID functionId,
        COR_PRF_TRANSITION_REASON reason) noexcept;

    HRESULT ExceptionUnwindFunctionLeave() noexcept;

private:
    SharedResource<FunctionStorage> m_functionStorage;

    FunctionInfo *m_pUnmanagedFunctionInfo;
    FunctionInfo *m_pJitFunctionInfo;
};

#endif // _EXECUTION_TRACE_H_
