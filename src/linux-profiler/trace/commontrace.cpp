#include <memory>
#include <utility>
#include <system_error>
#include <exception>
#include <stdexcept>

#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>

#include <winerror.h>

#include "profiler.h"
#include "intervalsplitter.h"
#include "commontrace.h"

#define CONTROL_SIGNAL_MIN (SIGRTMIN + 4)
#define LOG_SIGNAL         (CONTROL_SIGNAL_MIN + 0)
#define LOG_SIGNAL_STOP    (CONTROL_SIGNAL_MIN + 1)
#define SAMPLE_SIGNAL      (CONTROL_SIGNAL_MIN + 2)
#define SAMPLING_PAUSE     (CONTROL_SIGNAL_MIN + 3)
#define SAMPLING_RESUME    (CONTROL_SIGNAL_MIN + 4)
#define SAMPLING_EVENT     (CONTROL_SIGNAL_MIN + 5)
#define SAMPLING_STOP      (CONTROL_SIGNAL_MIN + 6)
#define CONTROL_SIGNAL_END (CONTROL_SIGNAL_MIN + 7)
#define CONTROL_SIGNAL_MAX (CONTROL_SIGNAL_END - 1)

// NOTE: currently only one instance of the CommonTrace can exist at each
// moment, so global variable can be used.
static CommonTrace *g_pCommonTraceObject = nullptr;

static void SampleHandlerStub(
    int code, siginfo_t *siginfo, void *context)
{
    if (code != SAMPLE_SIGNAL)
    {
        return;
    }

    CommonTrace *trace =
        reinterpret_cast<CommonTrace*>(siginfo->si_value.sival_ptr);

    if (trace != nullptr && trace->IsEnabled())
    {
        int terrno = errno;
        trace->HandleSample(context);
        errno = terrno;
    }
}

static void SamplingPauseResumeHandlerStub(int code)
{
    bool shouldPause;

    if (code == SAMPLING_PAUSE)
    {
        shouldPause = true;
    }
    else if (code == SAMPLING_RESUME)
    {
        shouldPause = false;
    }
    else
    {
        return;
    }

    if (g_pCommonTraceObject != nullptr && g_pCommonTraceObject->IsEnabled())
    {
        int terrno = errno;
        g_pCommonTraceObject->HandleSamplingPauseResume(shouldPause);
        errno = terrno;
    }
}

static struct timespec MsToTS(unsigned long ms)
{
    return { ms / 1000, ms % 1000 * 1000000 };
}

CommonTrace::CommonTrace(Profiler &profiler)
    : BaseTrace(profiler)
    , m_tlsThreadInfoIndex(TLS_OUT_OF_INDEXES)
    , m_threadStorage()
    , m_classStorage()
    , m_pauseAction()
    , m_resumeAction()
    , m_sampleAction()
    , m_logThread()
    , m_samplingThread()
    , m_samplingSuspended(true)
{
    _ASSERTE(g_pCommonTraceObject == nullptr);
    g_pCommonTraceObject = this;
}

CommonTrace::~CommonTrace()
{
    // NOTE: we are dealing with a partially destroyed m_profiler!
    this->Shutdown();

    if (m_tlsThreadInfoIndex != TLS_OUT_OF_INDEXES)
    {
        if (!TlsFree(m_tlsThreadInfoIndex))
        {
            m_profiler.HandleHresult(
                "CommonTrace::~CommonTrace(): TlsFree()",
                HRESULT_FROM_WIN32(GetLastError())
            );
        }
    }

    _ASSERTE(g_pCommonTraceObject == this);
    g_pCommonTraceObject = nullptr;
}

void CommonTrace::ProcessConfig(ProfilerConfig &config)
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
    // Performe runtime checks.
    //

    if (CONTROL_SIGNAL_MAX > SIGRTMAX)
    {
        throw std::runtime_error(
            "CommonTrace::ProcessConfig(): Not enought real-time signals");
    }

    //
    // Line Tracing.
    //

#ifndef _TARGET_ARM_
    if (config.LineTraceEnabled)
    {
        config.LineTraceEnabled = false;
        LOG().Warn() <<
            "Line tracing currently is not supported at this platform";
    }
#endif // _TARGET_ARM_

    //
    // Initializing thread local storage.
    //

    m_tlsThreadInfoIndex = TlsAlloc();
    if (m_tlsThreadInfoIndex == TLS_OUT_OF_INDEXES)
    {
        throw HresultException(
            "CommonTrace::ProcessConfig(): TlsAlloc()",
            HRESULT_FROM_WIN32(GetLastError())
        );
    }

    //
    // Setup signal handlers.
    //

    try
    {
        struct sigaction action;
        memset(&action, 0, sizeof(struct sigaction));
        action.sa_handler = SamplingPauseResumeHandlerStub;
        sigemptyset(&action.sa_mask);
        sigaddset(&action.sa_mask, SAMPLING_PAUSE);
        sigaddset(&action.sa_mask, SAMPLING_RESUME);
        action.sa_flags = SA_RESTART;

        SigAction pauseAction  ( SAMPLING_PAUSE,  action );
        SigAction resumeAction ( SAMPLING_RESUME, action );

        m_pauseAction  = std::move(pauseAction);
        m_resumeAction = std::move(resumeAction);
    }
    catch (const std::exception &e)
    {
        m_profiler.HandleException(e);
        LOG().Warn() << "Tracing pause/resume functionality is disabled";
    }

    if (config.HighGranularityEnabled)
    {
        try
        {
            struct sigaction action;
            memset(&action, 0, sizeof(struct sigaction));
            action.sa_sigaction = SampleHandlerStub;
            sigemptyset(&action.sa_mask);
            action.sa_flags = SA_RESTART | SA_SIGINFO;
            m_sampleAction = SigAction(SAMPLE_SIGNAL, action);
        }
        catch (const std::exception &e)
        {
            m_profiler.HandleException(e);
            config.HighGranularityEnabled = false;
            LOG().Warn() << "Hight granularity option is disabled";
        }
    }

    //
    // Starting service threads.
    //

    m_samplingSuspended = config.TracingSuspendedOnStart;

    {
        binary_semaphore threadInitializedSem;
        m_logThread = std::thread(
            &CommonTrace::LogThread, this,
            &threadInitializedSem
        );
        threadInitializedSem.wait();

        if (config.CollectionMethod == CollectionMethod::Sampling)
        {
            m_samplingThread = std::thread(
                &CommonTrace::SamplingThread, this,
                &threadInitializedSem
            );
            threadInitializedSem.wait();
        }
    }

    //
    // Event Mask calculation.
    //

    HRESULT hr;
    DWORD events;
    hr = m_info.v1()->GetEventMask(&events);
    if (FAILED(hr))
    {
        throw HresultException(
            "CommonTrace::ProcessConfig(): GetEventMask()", hr);
    }

    events = events
        | COR_PRF_MONITOR_APPDOMAIN_LOADS
        | COR_PRF_MONITOR_ASSEMBLY_LOADS
        | COR_PRF_MONITOR_MODULE_LOADS
        | COR_PRF_MONITOR_CLASS_LOADS
        | COR_PRF_MONITOR_THREADS;

    // This events are required for tracing of call stack dynamics.

    if (config.LineTraceEnabled)
    {
        events |= COR_PRF_ENABLE_STACK_SNAPSHOT;
    }

    hr = m_info.v1()->SetEventMask(events);
    if (FAILED(hr))
    {
        throw HresultException(
            "CommonTrace::ProcessConfig(): SetEventMask()", hr);
    }
}

void CommonTrace::Shutdown() noexcept
{
    m_disabled = true;

    // Ensure service threads are joined before this object will be destroyed.
    if (m_samplingThread.joinable())
    {
        this->SendStopSampling();
        m_samplingThread.join();
    }
    if (m_logThread.joinable())
    {
        this->SendStopLog();
        m_logThread.join();
    }

    // Restore signal handlers to defaults.
    m_pauseAction  . Release();
    m_resumeAction . Release();
    m_sampleAction . Release();
}

__forceinline void CommonTrace::SendDoSample(ThreadInfo &thrInfo) noexcept
{
    union sigval val;
    val.sival_ptr = this;
    int ev = pthread_sigqueue(thrInfo.nativeHandle, SAMPLE_SIGNAL, val);
    // It is OK if the limit of signals which may be queued has been reached.
    if (ev && ev != EAGAIN)
    {
        m_profiler.HandleSysErr(
            "CommonTrace::SendDoStackTraceSample(): pthread_sigqueue()", ev);
    }
}

__forceinline void CommonTrace::SendDoLog(ThreadInfo &thrInfo) noexcept
{
    _ASSERTE(!m_disabled);
    _ASSERTE(m_logThread.joinable());

    union sigval val;
    val.sival_ptr = &thrInfo;
    int ev = pthread_sigqueue(m_logThread.native_handle(), LOG_SIGNAL, val);
    // It is OK if the limit of signals which may be queued has been reached.
    if (ev && ev != EAGAIN)
    {
        m_profiler.HandleSysErr(
            "CommonTrace::SendDoLog(): pthread_sigqueue()", ev);
    }
}

__forceinline void CommonTrace::SendStopLog() noexcept
{
    _ASSERTE(m_logThread.joinable());

    int ev = pthread_kill(m_logThread.native_handle(), LOG_SIGNAL_STOP);
    if (ev)
    {
        m_profiler.HandleSysErr(
            "CommonTrace::SendStopLog(): pthread_kill()", ev);
    }
}

__forceinline void CommonTrace::SendSamplingEvent(SamplingEvent event) noexcept
{
    _ASSERTE(!m_disabled);
    _ASSERTE(m_samplingThread.joinable());

    union sigval val;
    val.sival_int = static_cast<int>(event);
    int ev = pthread_sigqueue(
        m_samplingThread.native_handle(), SAMPLING_EVENT, val);
    // It is OK if the limit of signals which may be queued has been reached.
    if (ev && ev != EAGAIN)
    {
        m_profiler.HandleSysErr(
            "CommonTrace::SendSamplingEvent(): pthread_sigqueue()", ev);
    }
}

__forceinline void CommonTrace::SendStopSampling() noexcept
{
    _ASSERTE(m_samplingThread.joinable());

    int ev = pthread_kill(m_samplingThread.native_handle(), SAMPLING_STOP);
    if (ev)
    {
        m_profiler.HandleSysErr(
            "CommonTrace::SendStopSampling(): pthread_kill()", ev);
    }
}

void CommonTrace::LogThread(binary_semaphore *pInitialized) noexcept
{
    try
    {
        //
        // Initialization.
        //

        int ev;
        sigset_t set;
        try
        {
            sigemptyset(&set);
            sigaddset(&set, LOG_SIGNAL);
            sigaddset(&set, LOG_SIGNAL_STOP);
            ev = pthread_sigmask(SIG_BLOCK, &set, NULL);
            if (ev)
            {
                throw std::system_error(ev, std::system_category(),
                    "CommonTrace::LogThread(): pthread_sigmask()");
            }

            pInitialized->notify();
            // NOTE: semaphore can be destroyed after notification.
            pInitialized = nullptr;
        }
        catch (const std::exception &e)
        {
            pInitialized->notify();
            m_profiler.HandleException(e);
            return;
        }

        //
        // Working loop.
        //

        for (;;)
        {
            siginfo_t siginfo;
            sigwaitinfo(&set, &siginfo);
            if (siginfo.si_signo == LOG_SIGNAL_STOP)
            {
                break;
            }
            else if (siginfo.si_signo != LOG_SIGNAL)
            {
                continue;
            }

            _ASSERTE(siginfo.si_signo == LOG_SIGNAL);

            ThreadInfo *pThreadInfo =
                reinterpret_cast<ThreadInfo*>(siginfo.si_value.sival_ptr);
            for (
                // Local copy of volatile data.
                size_t count = pThreadInfo->eventChannel.GetEventSummaryCount();
                count > 0; --count)
            {
                const EventSummary &summary =
                    pThreadInfo->eventChannel.GetCurrentEventSummary();
                TRACE().DumpSample(pThreadInfo->internalId, summary);
                pThreadInfo->eventChannel.NextEventSummary();
            }
        }
    }
    catch (const std::exception &e)
    {
        m_profiler.HandleException(e);
    }
}

void CommonTrace::SamplingThread(binary_semaphore *pInitialized) noexcept
{
    try
    {
        //
        // Initialization.
        //

        int ev;
        sigset_t set;
        try
        {
            sigemptyset(&set);
            sigaddset(&set, SAMPLING_EVENT);
            sigaddset(&set, SAMPLING_STOP);
            ev = pthread_sigmask(SIG_BLOCK, &set, NULL);
            if (ev)
            {
                throw std::system_error(ev, std::system_category(),
                    "CommonTrace::SamplingThread(): pthread_sigmask()");
            }

            pInitialized->notify();
            // NOTE: semaphore can be destroyed after notification.
            pInitialized = nullptr;
        }
        catch (const std::exception &e)
        {
            pInitialized->notify();
            m_profiler.HandleException(e);
            return;
        }

        //
        // Working loop.
        //

        IntervalSplitter splitter(m_profiler.GetConfig().SamplingTimeoutMs);
        ThreadStorage::LiveContainer liveThreads;
        ThreadStorage::LiveContainer::iterator  itThrInfo = liveThreads.begin();
        ThreadStorage::LiveContainer::iterator endThrInfo = liveThreads.end();

        for (;;)
        {
            siginfo_t siginfo;
            int rv;
            if (!m_samplingSuspended)
            {
                struct timespec ts;

                if (itThrInfo == endThrInfo)
                {
                    {
                        auto storage_lock = this->GetThreadStorage();
                        liveThreads = storage_lock->GetLiveContainer();
                    }
                    itThrInfo  = liveThreads.begin();
                    endThrInfo = liveThreads.end();
                    splitter.Reset(liveThreads.size());
                }

                if (!liveThreads.empty())
                {
                    auto storage_lock = this->GetThreadStorage();
                    ThreadInfo &thrInfo = *itThrInfo++;

                    // We update all live threads if they are attached to OS
                    // threads.
                    if (thrInfo.id != 0 && thrInfo.nativeHandle != 0)
                    {
                        thrInfo.genTicks++; // OK with unsigned overflows.
                        if (m_profiler.GetConfig().HighGranularityEnabled)
                        {
                            this->SendDoSample(thrInfo);
                        }
                    }

                    ts = MsToTS(splitter.GetNext());
                }
                else
                {
                    ts = MsToTS(m_profiler.GetConfig().SamplingTimeoutMs);
                }

                // NOTE: Sleep() function has better precision so we use it
                // for short pauses.
                if (ts.tv_sec == 0)
                {
                    Sleep(ts.tv_nsec / 1000000);
                    ts.tv_nsec = 0;
                }
                rv = sigtimedwait(&set, &siginfo, &ts);
            }
            else
            {
                rv = sigwaitinfo(&set, &siginfo);
            }

            if (rv == -1 && errno == EAGAIN)
            {
                continue;
            }
            else if (rv == SAMPLING_EVENT)
            {
                SamplingEvent event = static_cast<SamplingEvent>(
                    siginfo.si_value.sival_int);
                switch (event)
                {
                case SamplingEvent::SAMPLING_EVENT_PAUSE:
                    if (m_samplingSuspended == false)
                    {
                        TRACE().DumpProfilerTracingPause(
                            m_profiler.GetTickCountFromInit());
                    }
                    m_samplingSuspended = true;
                    break;

                case SamplingEvent::SAMPLING_EVENT_RESUME:
                    if (m_samplingSuspended == true)
                    {
                        TRACE().DumpProfilerTracingResume(
                            m_profiler.GetTickCountFromInit());
                        // Should restart threads round.
                        itThrInfo = endThrInfo;
                    }
                    m_samplingSuspended = false;
                    break;
                }

            }
            else if (rv == SAMPLING_STOP)
            {
                break; // End of loop!
            }
            else
            {
                m_profiler.HandleSysErr(
                    "CommonTrace::SamplingThread(): sigtimedwait()", errno);
            }
        }
    }
    catch (const std::exception &e)
    {
        m_profiler.HandleException(e);
    }
}

__forceinline void CommonTrace::DoSampleWithAction(
    ThreadInfo &thrInfo,
    SamplingAction action,
    SamplingSharedState &state) noexcept
{
    _ASSERTE(!thrInfo.interruptible);

    ExecutionTrace &executionTrace = m_profiler.GetExecutionTrace();
    MemoryTrace    &memoryTrace    = m_profiler.GetMemoryTrace();

    state.genTicks = thrInfo.genTicks; // Local copy of volatile data.
    bool needSample = executionTrace . NeedSample(thrInfo, state) ||
                      memoryTrace    . NeedSample(thrInfo, state);

    if (needSample)
    {
        executionTrace . PrepareSample(thrInfo, state);
        // memoryTrace    . PrepareSample(thrInfo, state);
    }

    if (action)
    {
        action(thrInfo, state);
    }

    if (needSample)
    {
        state.isSampleSucceeds = thrInfo.eventChannel.Sample(
            m_profiler.GetTickCountFromInit(),
            // OK with unsigned overflows in ticks.
            state.genTicks - thrInfo.fixTicks);
        if (state.isSampleSucceeds)
        {
            this->SendDoLog(thrInfo);
        }
        executionTrace . AfterSample(thrInfo, state);
        // memoryTrace    . AfterSample(thrInfo, state);
        thrInfo.fixTicks = state.genTicks;
    }
}

__forceinline void CommonTrace::DoSampleFromHandler(
    ThreadInfo &thrInfo, void *context) noexcept
{
    _ASSERTE(thrInfo.interruptible);

    SamplingSharedState state = {};
    state.context = context;

    ExecutionTrace &executionTrace = m_profiler.GetExecutionTrace();
    MemoryTrace    &memoryTrace    = m_profiler.GetMemoryTrace();

    state.genTicks = thrInfo.genTicks; // Local copy of volatile data.
    bool needSample = executionTrace . NeedSample(thrInfo, state) ||
                      memoryTrace    . NeedSample(thrInfo, state);

    if (needSample)
    {
        executionTrace . PrepareSample(thrInfo, state);
        // memoryTrace    . PrepareSample(thrInfo, state);

        state.isSampleSucceeds = thrInfo.eventChannel.Sample(
            m_profiler.GetTickCountFromInit(),
            // OK with unsigned overflows in ticks.
            state.genTicks - thrInfo.fixTicks,
            // NOTE: we can't reallocate memory from signal handler.
            ChanCanRealloc::NO);
        if (state.isSampleSucceeds)
        {
            this->SendDoLog(thrInfo);
        }

        executionTrace . AfterSample(thrInfo, state);
        // memoryTrace    . AfterSample(thrInfo, state);
        thrInfo.fixTicks = state.genTicks;
    }
}

ThreadInfo *CommonTrace::GetThreadInfo() noexcept
{
    try {
        //
        // Try to get thread info from the local storage.
        //

        ThreadInfo *threadInfo = reinterpret_cast<ThreadInfo*>(
            TlsGetValue(m_tlsThreadInfoIndex));

        if (threadInfo == nullptr)
        {
            DWORD lastError = GetLastError();
            if (lastError != ERROR_SUCCESS)
            {
                m_profiler.HandleHresult(
                    "CommonTrace::GetThreadInfo(): TlsGetValue()",
                    HRESULT_FROM_WIN32(lastError)
                );
            }
        }

        HRESULT hr;

        //
        // Fast check if current thread is changed.
        //

        ThreadID threadId = 0;
        hr = m_info.v1()->GetCurrentThreadID(&threadId);
        if (FAILED(hr))
        {
            throw HresultException(
                "CommonTrace::GetThreadInfo(): GetCurrentThreadID()", hr);
        }

        if (threadInfo == nullptr || threadInfo->id != threadId)
        {
            //
            // We should update thread info.
            //

            // Get or create thread info for current thread ID.
            ThreadInfo *oldThreadInfo = threadInfo;
            auto storage_lock = m_threadStorage.lock();
            threadInfo = &storage_lock->Place(threadId).first;

            // Get current OS thread ID.
            DWORD osThreadId = 0;
            hr = m_info.v1()->GetThreadInfo(threadId, &osThreadId);
            // This is OK if we can't obtain osThreadId in some special cases.
            if (FAILED(hr) && hr != CORPROF_E_UNSUPPORTED_CALL_SEQUENCE)
            {
                m_profiler.HandleHresult(
                    "CommonTrace::GetThreadInfo(): GetThreadInfo()", hr);
            }

            // Check if OS thread ID changed and update it.
            if (oldThreadInfo != nullptr &&
                oldThreadInfo->osThreadId == osThreadId)
            {
                oldThreadInfo->osThreadId   = 0;
                oldThreadInfo->nativeHandle = 0;
            }
            threadInfo->osThreadId   = osThreadId;
            threadInfo->nativeHandle = pthread_self();

            //
            // Save new thead info to the local storage.
            //

            if (!TlsSetValue(m_tlsThreadInfoIndex, threadInfo))
            {
                m_profiler.HandleHresult(
                    "CommonTrace::GetThreadInfo(): TlsSetValue()",
                    HRESULT_FROM_WIN32(GetLastError())
                );
            }
        }

        return threadInfo;
    }
    catch (const std::exception &e)
    {
        m_profiler.HandleException(e);
        return nullptr;
    }
}

ThreadInfo *CommonTrace::GetThreadInfoR() const noexcept
{
    //
    // Try to get thread info from the local storage.
    //

    ThreadInfo *threadInfo = reinterpret_cast<ThreadInfo*>(
        TlsGetValue(m_tlsThreadInfoIndex));

#ifdef _TARGET_AMD64_
    if (threadInfo == nullptr)
    {
        return nullptr;
    }

    //
    // Fast check if current thread is changed.
    //

    HRESULT hr;
    ThreadID threadId = 0;
    hr = m_info.v1()->GetCurrentThreadID(&threadId);
    if (FAILED(hr) || threadInfo->id != threadId)
    {
        return nullptr;
    }
#endif // _TARGET_AMD64_

    return threadInfo;
}

void CommonTrace::InterruptSampling(
    SamplingSharedState &state,
    SamplingAction      beforeAction,
    SamplingAction      action,
    SamplingAction      afterAction) noexcept
{
    ThreadInfo *pThreadInfo = m_profiler.GetCommonTrace().GetThreadInfo();
    if (pThreadInfo != nullptr)
    {
        pThreadInfo->interruptible = false;

        this->DoSampleWithAction(*pThreadInfo, beforeAction, state);

        if (action)
        {
            action(*pThreadInfo, state);
        }

        this->DoSampleWithAction(*pThreadInfo, afterAction, state);

        pThreadInfo->interruptible = true;
    }
}

__forceinline void CommonTrace::HandleSample(void *context) noexcept
{
    _ASSERTE(!m_disabled);
    ThreadInfo *pThreadInfo = this->GetThreadInfoR();
    if (pThreadInfo && pThreadInfo->interruptible)
    {
        DoSampleFromHandler(*pThreadInfo, context);
    }
}

__forceinline void CommonTrace::HandleSamplingPauseResume(
    bool shouldPause) noexcept
{
    _ASSERTE(!m_disabled);
    if (m_profiler.GetConfig().CollectionMethod == CollectionMethod::Sampling)
    {
        this->SendSamplingEvent(
            shouldPause ? SamplingEvent::SAMPLING_EVENT_PAUSE :
                          SamplingEvent::SAMPLING_EVENT_RESUME
        );
    }
    else if (m_profiler.GetConfig().CollectionMethod ==
        CollectionMethod::Instrumentation)
    {
        m_samplingSuspended = shouldPause;
    }
}

bool CommonTrace::IsSamplingSuspended() const noexcept
{
    return m_samplingSuspended;
}

HRESULT CommonTrace::AppDomainCreationFinished(
    AppDomainID appDomainId,
    HRESULT hrStatus) noexcept
{
    if (m_disabled)
        return S_OK;

    HRESULT hr = S_OK;
    try
    {
        ULONG     size = 0;
        ProcessID processId = 0;

        hr = m_info.v1()->GetAppDomainInfo(
            appDomainId, 0, &size, nullptr, &processId);

        std::unique_ptr<WCHAR[]> name = nullptr;
        if (SUCCEEDED(hr))
        {
            name.reset(new (std::nothrow) WCHAR[size]);
            if (name)
            {
                hr = m_info.v1()->GetAppDomainInfo(
                    appDomainId, size, nullptr, name.get(), nullptr);
            }
        }

        TRACE().DumpAppDomainCreationFinished(
            appDomainId, name.get(), processId, hrStatus);

        // Do it after dump.
        if (FAILED(hr))
        {
            throw HresultException(
                "CommonTrace::AppDomainCreationFinished()", hr);
        }
    }
    catch (const std::exception &e)
    {
        hr = m_profiler.HandleException(e);
    }

    return hr;
}

HRESULT CommonTrace::AssemblyLoadFinished(
    AssemblyID assemblyId,
    HRESULT hrStatus) noexcept
{
    if (m_disabled)
        return S_OK;

    HRESULT hr = S_OK;
    try
    {
        ULONG      size = 0;
        AssemblyID appDomainId = 0;
        ModuleID   moduleId = 0;

        hr = m_info.v1()->GetAssemblyInfo(
            assemblyId, 0, &size, nullptr, &appDomainId, &moduleId);

        std::unique_ptr<WCHAR[]> name = nullptr;
        if (SUCCEEDED(hr))
        {
            name.reset(new (std::nothrow) WCHAR[size]);
            if (name)
            {
                hr = m_info.v1()->GetAssemblyInfo(
                    assemblyId, size, nullptr, name.get(), nullptr, nullptr);
            }
        }

        TRACE().DumpAssemblyLoadFinished(
            assemblyId, name.get(), appDomainId, moduleId, hrStatus);

        // Do it after dump.
        if (FAILED(hr))
        {
            throw HresultException(
                "CommonTrace::AssemblyLoadFinished(): GetAssemblyInfo()", hr);
        }
    }
    catch (const std::exception &e)
    {
        hr = m_profiler.HandleException(e);
    }

    return hr;
}

HRESULT CommonTrace::ModuleLoadFinished(
    ModuleID moduleId,
    HRESULT hrStatus) noexcept
{
    if (m_disabled)
        return S_OK;

    HRESULT hr = S_OK;
    try
    {
        ULONG      size = 0;
        LPCBYTE    baseLoadAddress = 0;
        AssemblyID assemblyId = 0;

        hr = m_info.v1()->GetModuleInfo(
            moduleId, &baseLoadAddress, 0, &size, nullptr, &assemblyId);

        std::unique_ptr<WCHAR[]> name = nullptr;
        if (SUCCEEDED(hr))
        {
            name.reset(new (std::nothrow) WCHAR[size]);
            if (name)
            {
                hr = m_info.v1()->GetModuleInfo(
                    moduleId, nullptr, size, nullptr, name.get(), nullptr);
            }
        }

        TRACE().DumpModuleLoadFinished(
            moduleId, baseLoadAddress, name.get(), assemblyId, hrStatus);

        // Do it after dump.
        if (FAILED(hr))
        {
            throw HresultException(
                "CommonTrace::ModuleLoadFinished(): GetModuleInfo()", hr);
        }
    }
    catch (const std::exception &e)
    {
        hr = m_profiler.HandleException(e);
    }

    return hr;
}

HRESULT CommonTrace::ModuleAttachedToAssembly(
    ModuleID moduleId,
    AssemblyID assemblyId) noexcept
{
    if (m_disabled)
        return S_OK;

    HRESULT hr = S_OK;
    try
    {
        TRACE().DumpModuleAttachedToAssembly(moduleId, assemblyId);
    }
    catch (const std::exception &e)
    {
        hr = m_profiler.HandleException(e);
    }

    return hr;
}

HRESULT CommonTrace::ClassLoadStarted(
    ClassID classId) noexcept
{
    if (m_disabled)
        return S_OK;

    HRESULT hr = S_OK;
    try
    {
        m_classStorage.lock()->Place(classId);
    }
    catch (const std::exception &e)
    {
        hr = m_profiler.HandleException(e);
    }

    return hr;
}

HRESULT CommonTrace::ClassLoadFinished(
    ClassID classId,
    HRESULT hrStatus) noexcept
{
    if (m_disabled)
        return S_OK;

    if (m_info.v1()->IsArrayClass(classId, nullptr, nullptr, nullptr) == S_OK)
    {
        LOG().Warn() << "Array class in ClassLoadFinished()";
    }

    HRESULT hr = S_OK;
    try
    {
        auto storage_lock = m_classStorage.lock();
        ClassInfo &classInfo = storage_lock->Get(classId);
        hr = classInfo.Initialize(m_profiler, *storage_lock);

        TRACE().DumpClassLoadFinished(classInfo, hrStatus);
        if (!classInfo.isNamePrinted)
        {
            TRACE().DumpClassName(classInfo);
            classInfo.isNamePrinted = true;
        }
    }
    catch (const std::exception &e)
    {
        hr = m_profiler.HandleException(e);
    }

    return hr;
}

HRESULT CommonTrace::ClassUnloadStarted(
    ClassID classId) noexcept
{
    if (m_disabled)
        return S_OK;

    HRESULT hr = S_OK;
    try
    {
        m_classStorage.lock()->Unlink(classId);
    }
    catch (const std::exception &e)
    {
        hr = m_profiler.HandleException(e);
    }

    return hr;
}

HRESULT CommonTrace::ThreadCreated(
    ThreadID threadId) noexcept
{
    if (m_disabled)
        return S_OK;

    HRESULT hr = S_OK;
    try
    {
        InternalID threadIid =
            m_threadStorage.lock()->Place(threadId).first.internalId;
        TRACE().DumpThreadCreated(threadId, threadIid);
    }
    catch (const std::exception &e)
    {
        hr = m_profiler.HandleException(e);
    }

    return hr;
}

HRESULT CommonTrace::ThreadDestroyed(
    ThreadID threadId) noexcept
{
    if (m_disabled)
        return S_OK;

    HRESULT hr = S_OK;
    try
    {
        InternalID threadIid;
        {
            auto storage_lock = m_threadStorage.lock();
            ThreadInfo &thrInfo = storage_lock->Unlink(threadId);
            thrInfo.osThreadId = 0;
            threadIid = thrInfo.internalId;
        }
        TRACE().DumpThreadDestroyed(threadIid);
    }
    catch (const std::exception &e)
    {
        hr = m_profiler.HandleException(e);
    }

    return hr;
}

HRESULT CommonTrace::ThreadAssignedToOSThread(
    ThreadID managedThreadId,
    DWORD osThreadId) noexcept
{
    if (m_disabled)
        return S_OK;

    HRESULT hr = S_OK;
    try
    {
        InternalID threadIid;
        {
            auto storage_lock = m_threadStorage.lock();
            ThreadInfo &thrInfo = storage_lock->Get(managedThreadId);

            if (thrInfo.osThreadId != osThreadId)
            {
                // Get current OS thread ID.
                DWORD currentOsThreadId = 0;
                hr = m_info.v1()->GetThreadInfo(
                    managedThreadId, &currentOsThreadId);
                // Check if we can setup OS thread ID.
                if (SUCCEEDED(hr) && currentOsThreadId == osThreadId)
                {
                    thrInfo.nativeHandle = pthread_self();
                }
                else
                {
                    // This will be updated by GetThreadInfo() later.
                    thrInfo.nativeHandle = 0;
                    if (FAILED(hr))
                    {
                        m_profiler.HandleHresult(
                            "CommonTrace::ThreadAssignedToOSThread(): "
                            "GetThreadInfo()", hr
                        );
                    }
                }
                thrInfo.osThreadId = osThreadId;
            }
            threadIid = thrInfo.internalId;
        }
        TRACE().DumpThreadAssignedToOSThread(threadIid, osThreadId);
    }
    catch (const std::exception &e)
    {
        hr = m_profiler.HandleException(e);
    }

    return hr;
}

auto CommonTrace::GetThreadStorage() ->
    decltype(m_threadStorage.lock())
{
    return m_threadStorage.lock();
}

auto CommonTrace::GetThreadStorage() const ->
    decltype(m_threadStorage.lock_shared())
{
    return m_threadStorage.lock_shared();
}

auto CommonTrace::GetClassStorage() ->
    decltype(m_classStorage.lock())
{
    return m_classStorage.lock();
}

auto CommonTrace::GetClassStorage() const ->
    decltype(m_classStorage.lock_shared())
{
    return m_classStorage.lock_shared();
}
