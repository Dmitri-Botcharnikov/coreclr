#include <exception>
#include <tuple>

#include "profiler.h"
#include "executiontrace.h"

EXTERN_C UINT_PTR __stdcall FunctionIDMapStub(
    FunctionID funcId,
    void *clientData,
    BOOL *pbHookFunction)
{
    return reinterpret_cast<ExecutionTrace*>(clientData)->
        FunctionIDMap(funcId, pbHookFunction);
}

EXTERN_C void EnterNaked3(FunctionIDOrClientID functionIDOrClientID);
EXTERN_C void LeaveNaked3(FunctionIDOrClientID functionIDOrClientID);
EXTERN_C void TailcallNaked3(FunctionIDOrClientID functionIDOrClientID);

#ifdef _TARGET_ARM_
EXTERN_C UINT_PTR getPrevPC();
#endif // _TARGET_ARM_

EXTERN_C void __stdcall EnterStub(FunctionIDOrClientID functionIDOrClientID)
{
    UINT_PTR ip = 0;

#ifdef _TARGET_ARM_
    ip = getPrevPC();
#endif // _TARGET_ARM_

    FunctionInfo *funcInfo = reinterpret_cast<FunctionInfo*>(
        functionIDOrClientID.clientID);
    funcInfo->executionTrace->Enter(*funcInfo, ip);
}

EXTERN_C void __stdcall LeaveStub(FunctionIDOrClientID functionIDOrClientID)
{
    FunctionInfo *funcInfo = reinterpret_cast<FunctionInfo*>(
        functionIDOrClientID.clientID);
    funcInfo->executionTrace->Leave(*funcInfo);
}

EXTERN_C void __stdcall TailcallStub(FunctionIDOrClientID functionIDOrClientID)
{
    FunctionInfo *funcInfo = reinterpret_cast<FunctionInfo*>(
        functionIDOrClientID.clientID);
    funcInfo->executionTrace->Tailcall(*funcInfo);
}

ExecutionTrace::ExecutionTrace(Profiler &profiler)
    : BaseTrace(profiler)
    , m_functionStorage(this)
    , m_pUnmanagedFunctionInfo(nullptr)
    , m_pJitFunctionInfo(nullptr)
{
    auto storage_lock = m_functionStorage.lock();

    m_pUnmanagedFunctionInfo = &storage_lock->Add();
    m_pJitFunctionInfo       = &storage_lock->Add();

    m_pUnmanagedFunctionInfo->name     = W("<UNMANAGED>");
    m_pUnmanagedFunctionInfo->fullName = m_pUnmanagedFunctionInfo->name;
    m_pJitFunctionInfo->name           = W("<JIT>");
    m_pJitFunctionInfo->fullName       = m_pJitFunctionInfo->name;
}

ExecutionTrace::~ExecutionTrace()
{
    // NOTE: we are dealing with a partially destroyed m_profiler!
    this->Shutdown();
}

void ExecutionTrace::ProcessConfig(ProfilerConfig &config)
{
    //
    // Check activation condition.
    //

    if (config.ExecutionTraceEnabled || config.MemoryTraceEnabled)
    {
        m_disabled = false;
    }
    else
    {
        return;
    }

    //
    // Announce names of the special functions.
    //

    TRACE().DumpJITFunctionName(*m_pUnmanagedFunctionInfo);
    TRACE().DumpJITFunctionName(*m_pJitFunctionInfo);

    //
    // Event Mask calculation.
    //

    HRESULT hr;
    DWORD events;
    hr = m_info.v1()->GetEventMask(&events);
    if (FAILED(hr))
    {
        throw HresultException(
            "ExecutionTrace::ProcessConfig(): GetEventMask()", hr);
    }

    // This events are common for execution tracing.
    events = events
        | COR_PRF_MONITOR_JIT_COMPILATION
        | COR_PRF_MONITOR_CACHE_SEARCHES
        | COR_PRF_MONITOR_FUNCTION_UNLOADS;

    if (config.CollectionMethod == CollectionMethod::Instrumentation ||
        config.CollectionMethod == CollectionMethod::Sampling)
    {
        if (m_info.version() < 3)
        {
            LOG().Warn() <<
                "ICorProfilerInfo3 is required for current configuration";
            goto next_stage;
        }

        m_info.v3()->SetFunctionIDMapper2(FunctionIDMapStub, this);

        hr = m_info.v3()->SetEnterLeaveFunctionHooks3(
            EnterNaked3, LeaveNaked3, TailcallNaked3);
        if (FAILED(hr))
        {
            m_profiler.HandleHresult(
                "ExecutionTrace::ProcessConfig(): "
                "SetEnterLeaveFunctionHooks3()", hr
            );
            goto next_stage;
        }

        // This events are required for tracing of call stack dynamics.
        events = events
            | COR_PRF_MONITOR_ENTERLEAVE
            | COR_PRF_MONITOR_CODE_TRANSITIONS
            | COR_PRF_MONITOR_EXCEPTIONS;
    }

next_stage:
    //
    // Set Event Mask.
    //

    hr = m_info.v1()->SetEventMask(events);
    if (FAILED(hr))
    {
        throw HresultException(
            "ExecutionTrace::ProcessConfig(): SetEventMask()", hr);
    }
}

void ExecutionTrace::Shutdown() noexcept
{
    m_disabled = true;
}

bool ExecutionTrace::IsPseudoFunction(
    const FunctionInfo &funcInfo) const noexcept
{
    _ASSERTE(m_pUnmanagedFunctionInfo != nullptr);
    _ASSERTE(m_pUnmanagedFunctionInfo->internalId.id == 0);
    _ASSERTE(m_pJitFunctionInfo != nullptr);
    _ASSERTE(m_pJitFunctionInfo->internalId.id == 1);

    return funcInfo.internalId.id >= 0 && funcInfo.internalId.id <= 1;
}

UINT_PTR ExecutionTrace::GetCurrentManagedIP(
    ThreadInfo &thrInfo, CONTEXT *winContext) noexcept
{
    struct Snapshot {
        FunctionID funcId;
        UINT_PTR   pc;

        static HRESULT Callback(
            FunctionID funcId,
            UINT_PTR ip,
            COR_PRF_FRAME_INFO frameInfo,
            ULONG32 contextSize,
            BYTE context[],
            void *clientData)
        {
            Snapshot *data = reinterpret_cast<Snapshot*>(clientData);

            if (funcId == data->funcId)
            {
                // We have HIT !
                data->pc = ip;
                return E_FAIL; // != S_OK
            }

            return S_OK;
        }
    };

    const FunctionInfo &funcInfo =
        *thrInfo.eventChannel.GetFrameFromTop().pFuncInfo;
    if (this->IsPseudoFunction(funcInfo))
    {
        return 0;
    }

    // XXX: prevent deadlock at DoStackSnapshot() call.
    if (std::uncaught_exception())
    {
        return 0;
    }

    _ASSERTE(m_info.version() >= 2);
    Snapshot data = {funcInfo.id, 0};
    m_info.v2()->DoStackSnapshot(thrInfo.id, Snapshot::Callback,
        COR_PRF_SNAPSHOT_DEFAULT, &data,
        reinterpret_cast<BYTE*>(winContext), sizeof(winContext));

    return data.pc;
}

void ExecutionTrace::RestoreManagedIP(
    ThreadInfo &thrInfo, CONTEXT *winContext) noexcept
{
    struct Snapshot {
        ExecutionTrace &execTrace;
        EventChannel   &channel;
        size_t         idxFromTop;
        size_t         maxIdxFromTop;
        size_t         maxUndIdxFromTop;

        static HRESULT Callback(
            FunctionID funcId,
            UINT_PTR ip,
            COR_PRF_FRAME_INFO frameInfo,
            ULONG32 contextSize,
            BYTE context[],
            void *clientData)
        {
            Snapshot *data = reinterpret_cast<Snapshot*>(clientData);

            ExecutionTrace &execTrace    = data->execTrace;
            EventChannel   &channel      = data->channel;
            size_t         &idxFromTop   = data->idxFromTop; // Reference!
            size_t         maxIdxFromTop = data->maxIdxFromTop;
            size_t         &maxUndIdxFromTop =
                data->maxUndIdxFromTop; // Reference!

            _ASSERTE(maxIdxFromTop < channel.GetStackSize());
            _ASSERTE(idxFromTop <= maxIdxFromTop);

            // Skip pseudo-functions.
            const Frame *pFrame = &channel.GetFrameFromTop(idxFromTop);
            while (execTrace.IsPseudoFunction(*pFrame->pFuncInfo))
            {
                if (++idxFromTop > maxIdxFromTop)
                {
                    return E_FAIL; // Stop unwinding.
                }
                pFrame = &channel.GetFrameFromTop(idxFromTop);
            }

            if (funcId == pFrame->pFuncInfo->id)
            {
                // We have HIT !
                if (ip != 0 || idxFromTop == 0)
                {
                    channel.ChIP(ip, idxFromTop++);
                }
                else
                {
                    maxUndIdxFromTop = idxFromTop++;
                }
            }

            // Continue for next function on stack or stop unwinding.
            return idxFromTop <= maxIdxFromTop ? S_OK : E_FAIL;
        }
    };

    // XXX: prevent deadlock at DoStackSnapshot() call.
    if (std::uncaught_exception())
    {
        return;
    }

    _ASSERTE(m_info.version() >= 2);
    Snapshot data = {
        *this,
        thrInfo.eventChannel,
        0,
        thrInfo.maxRestoreIpIdx,
        0,
    };
    HRESULT hr;
    hr = m_info.v2()->DoStackSnapshot(thrInfo.id, Snapshot::Callback,
        COR_PRF_SNAPSHOT_DEFAULT, &data,
        reinterpret_cast<BYTE*>(winContext), sizeof(winContext));
    if (FAILED(hr) && hr != CORPROF_E_STACKSNAPSHOT_ABORTED)
    {
        thrInfo.eventChannel.ChIP(0);
    }
    else if (data.idxFromTop > data.maxIdxFromTop)
    {
        thrInfo.maxRestoreIpIdx = data.maxUndIdxFromTop;
    }
}

bool ExecutionTrace::NeedSample(
    ThreadInfo &thrInfo, SamplingSharedState &state) const noexcept
{
    if (m_disabled || !m_profiler.GetConfig().ExecutionTraceEnabled)
        return false;

    return (thrInfo.fixTicks != state.genTicks) ||
        (
            thrInfo.eventChannel.HasStackSample() &&
            (m_profiler.GetConfig().CollectionMethod ==
                CollectionMethod::Instrumentation &&
            !m_profiler.GetCommonTrace().IsSamplingSuspended())
        );
}

HRESULT ContextToStackSnapshotContext(
    const void *context, CONTEXT *winContext) noexcept;

void ExecutionTrace::PrepareSample(
    ThreadInfo &thrInfo, SamplingSharedState &state) noexcept
{
    if (m_profiler.GetConfig().LineTraceEnabled &&
        thrInfo.eventChannel.GetStackSize() > 0)
    {
        if (state.context)
        {
            CONTEXT winContext;
            if (SUCCEEDED(ContextToStackSnapshotContext(
                state.context, &winContext)))
            {
                this->RestoreManagedIP(thrInfo, &winContext);
            }
            else
            {
                thrInfo.eventChannel.ChIP(0);
            }
        }
        else
        {
            this->RestoreManagedIP(thrInfo);
        }
        state.isIpRestored = true;
    }
}

void ExecutionTrace::AfterSample(
    ThreadInfo &thrInfo, SamplingSharedState &state) noexcept
{
    if (state.isSampleSucceeds)
    {
        thrInfo.maxRestoreIpIdx = 0;
    }
}

HRESULT ContextToStackSnapshotContext(
    const void *context, CONTEXT *winContext) noexcept;

void ExecutionTrace::UpdateCallStackPush(const FunctionInfo &funcInfo) noexcept
{
    SamplingSharedState state = {};
    state.stackWillBeChanged = true;
    m_profiler.GetCommonTrace().InterruptSampling(
        state,
        {},
        [&funcInfo](ThreadInfo &thrInfo, SamplingSharedState &state)
        {
            EventChannel &channel = thrInfo.eventChannel;
            if (channel.GetStackSize() > 0)
            {
                ++thrInfo.maxRestoreIpIdx;
            }
            channel.Push(funcInfo);
            state.isIpRestored = false;
            state.stackWillBeChanged = false;
        }
    );
}

void ExecutionTrace::UpdateCallStackPush(
    const FunctionInfo &funcInfo, UINT_PTR prevIP) noexcept
{
    SamplingSharedState state = {};
    state.stackWillBeChanged = true;
    m_profiler.GetCommonTrace().InterruptSampling(
        state,
        [&funcInfo, prevIP](ThreadInfo &thrInfo, SamplingSharedState &state)
        {
            EventChannel &channel = thrInfo.eventChannel;
            if (channel.GetStackSize() > 0)
            {
                channel.ChIP(prevIP);
            }
        },
        [&funcInfo, prevIP](ThreadInfo &thrInfo, SamplingSharedState &state)
        {
            EventChannel &channel = thrInfo.eventChannel;
            if (channel.GetStackSize() > 0)
            {
                if (prevIP == 0 || thrInfo.maxRestoreIpIdx > 0)
                {
                    ++thrInfo.maxRestoreIpIdx;
                }
            }
            channel.Push(funcInfo);
            state.isIpRestored = false;
            state.stackWillBeChanged = false;
        }
    );
}

void ExecutionTrace::UpdateCallStackPop() noexcept
{
    SamplingSharedState state = {};
    state.stackWillBeChanged = true;
    m_profiler.GetCommonTrace().InterruptSampling(
        state,
        {},
        [](ThreadInfo &thrInfo, SamplingSharedState &state)
        {
            thrInfo.eventChannel.Pop();
            if (thrInfo.maxRestoreIpIdx > 0)
            {
                --thrInfo.maxRestoreIpIdx;
            }
            state.isIpRestored = false;
            state.stackWillBeChanged = false;
        }
    );
}

UINT_PTR ExecutionTrace::FunctionIDMap(
    FunctionID funcId,
    BOOL *pbHookFunction) noexcept
{
    LOG().Trace() << "FunctionIDMap()";

    try
    {
        FunctionInfo *pFuncInfo =
            &m_functionStorage.lock()->Place(funcId).first;
        pFuncInfo->executionTrace = this;
        *pbHookFunction = true;
        // This pointer should be stable during all lifetime of the Profiler.
        // It is important feature guaranteed by the BaseStorage class.
        return reinterpret_cast<UINT_PTR>(pFuncInfo);
    }
    catch (const std::exception &e)
    {
        m_profiler.HandleException(e);
        *pbHookFunction = false;
        return reinterpret_cast<UINT_PTR>(nullptr);
    }
}

__forceinline void ExecutionTrace::Enter(
    const FunctionInfo &funcInfo, UINT_PTR prevIP) noexcept
{
    LOG().Trace() << "EnterStub()";
    if (m_profiler.GetConfig().LineTraceEnabled)
    {
        this->UpdateCallStackPush(funcInfo, prevIP);
    }
    else
    {
        this->UpdateCallStackPush(funcInfo);
    }
}

__forceinline void ExecutionTrace::Leave(const FunctionInfo &funcInfo) noexcept
{
    LOG().Trace() << "LeaveStub()";
    this->UpdateCallStackPop();
}

__forceinline void ExecutionTrace::Tailcall(
    const FunctionInfo &funcInfo) noexcept
{
    LOG().Trace() << "TailcallStub()";
    this->UpdateCallStackPop();
}

HRESULT ExecutionTrace::JITStarted(
    FunctionID functionId) noexcept
{
    HRESULT hr = S_OK;
    try
    {
        this->UpdateCallStackPush(*m_pJitFunctionInfo);
        m_functionStorage.lock()->Place(functionId);
    }
    catch (const std::exception &e)
    {
        hr = m_profiler.HandleException(e);
    }

    return hr;
}

HRESULT ExecutionTrace::JITFinished(
    FunctionID functionId,
    std::function<JITDumpFunction> dumpFunction) noexcept
{
    HRESULT hr = S_OK;
    try
    {
        auto storage_lock = m_functionStorage.lock();
        FunctionInfo &funcInfo = storage_lock->Get(functionId);

        {
            auto storage_lock = m_profiler.GetCommonTrace().GetClassStorage();
            hr = funcInfo.Initialize(m_profiler, *storage_lock);
        }

        dumpFunction(funcInfo);
        if (!funcInfo.isNamePrinted)
        {
            TRACE().DumpJITFunctionName(funcInfo);
            funcInfo.isNamePrinted = true;
        }

        this->UpdateCallStackPop();
    }
    catch (const std::exception &e)
    {
        hr = m_profiler.HandleException(e);
    }

    return hr;
}

HRESULT ExecutionTrace::FunctionUnloadStarted(
    FunctionID functionId) noexcept
{
    if (m_disabled)
        return S_OK;

    HRESULT hr = S_OK;
    try
    {
        m_functionStorage.lock()->Unlink(functionId);
    }
    catch (const std::exception &e)
    {
        hr = m_profiler.HandleException(e);
    }

    return hr;
}

HRESULT ExecutionTrace::JITCompilationStarted(
    FunctionID functionId,
    BOOL fIsSafeToBlock) noexcept
{
    if (m_disabled)
        return S_OK;

    return this->JITStarted(functionId);
}

HRESULT ExecutionTrace::JITCompilationFinished(
    FunctionID functionId,
    HRESULT hrStatus, BOOL fIsSafeToBlock) noexcept
{
    if (m_disabled)
        return S_OK;

    return this->JITFinished(
        functionId,
        [this, hrStatus](const FunctionInfo &funcInfo)
        {
            TRACE().DumpJITCompilationFinished(funcInfo, hrStatus);
        }
    );
}

HRESULT ExecutionTrace::JITCachedFunctionSearchStarted(
    FunctionID functionId,
    BOOL *pbUseCachedFunction) noexcept
{
    if (m_disabled)
        return S_OK;

    *pbUseCachedFunction = TRUE;
    return this->JITStarted(functionId);
}

HRESULT ExecutionTrace::JITCachedFunctionSearchFinished(
    FunctionID functionId,
    COR_PRF_JIT_CACHE result) noexcept
{
    if (m_disabled)
        return S_OK;

    if (result != COR_PRF_CACHED_FUNCTION_FOUND)
    {
        return S_OK;
    }

    return this->JITFinished(
        functionId,
        [this](const FunctionInfo &funcInfo)
        {
            TRACE().DumpJITCachedFunctionSearchFinished(funcInfo);
        }
    );
}

HRESULT ExecutionTrace::UnmanagedToManagedTransition(
    FunctionID functionId,
    COR_PRF_TRANSITION_REASON reason) noexcept
{
    if (m_disabled)
        return S_OK;

    if (reason == COR_PRF_TRANSITION_RETURN)
    {
        this->UpdateCallStackPop();
    }

    return S_OK;
}

HRESULT ExecutionTrace::ManagedToUnmanagedTransition(
    FunctionID functionId,
    COR_PRF_TRANSITION_REASON reason) noexcept
{
    if (m_disabled)
        return S_OK;

    if (reason == COR_PRF_TRANSITION_CALL)
    {
        this->UpdateCallStackPush(*m_pUnmanagedFunctionInfo);
    }

    return S_OK;
}

HRESULT ExecutionTrace::ExceptionUnwindFunctionLeave() noexcept
{
    if (m_disabled)
        return S_OK;

    this->UpdateCallStackPop();

    return S_OK;
}
