#ifndef _COMMON_TRACE_H_
#define _COMMON_TRACE_H_

#include <thread>

#ifdef _TARGET_AMD64_
#include <future>
#endif // _TARGET_AMD64_

#include <cor.h>
#include <corhdr.h>
#include <corprof.h>

#include "basetrace.h"

#include "sharedresource.h"
#include "threadstorage.h"
#include "classstorage.h"
#include "sigaction.h"
#include "binarysemaphore.h"

class CommonTrace final : public BaseTrace
{
public:
    CommonTrace(Profiler &profiler);

    ~CommonTrace();

    void ProcessConfig(ProfilerConfig &config);

    void Shutdown() noexcept;

    struct SamplingSharedState
    {
        ULONG genTicks;
        bool  isIpRestored;
        bool  isSampleSucceeds;
        void  *context;
        bool  stackWillBeChanged;
    };

    typedef std::function<void(ThreadInfo&, SamplingSharedState&)>
        SamplingAction;

private:
    enum class SamplingEvent
    {
        SAMPLING_EVENT_PAUSE,
        SAMPLING_EVENT_RESUME,
    };

    void SendDoSample(ThreadInfo &thrInfo) noexcept;

    void SendDoLog(ThreadInfo &thrInfo) noexcept;

    void SendStopLog() noexcept;

    void SendSamplingEvent(SamplingEvent event) noexcept;

    void SendStopSampling() noexcept;

    void LogThread(binary_semaphore *pInitialized) noexcept;

    void SamplingThread(binary_semaphore *pInitialized) noexcept;

    void DoSampleWithAction(
        ThreadInfo &thrInfo,
        SamplingAction action,
        SamplingSharedState &state) noexcept;

    void DoSampleFromHandler(
        ThreadInfo &thrInfo, void *context) noexcept;

public:
    ThreadInfo *GetThreadInfo() noexcept;

    // Simple and safety version of GetThreadInfo() that can be used in signal
    // handlers.
    ThreadInfo *GetThreadInfoR() const noexcept;

    void InterruptSampling(
        SamplingSharedState &state,
        SamplingAction      beforeAction = {},
        SamplingAction      action       = {},
        SamplingAction      afterAction  = {}) noexcept;

    void HandleSample(void *context) noexcept;

    void HandleSamplingPauseResume(bool shouldPause) noexcept;

    bool IsSamplingSuspended() const noexcept;

    HRESULT AppDomainCreationFinished(
        AppDomainID appDomainId,
        HRESULT hrStatus) noexcept;

    HRESULT AssemblyLoadFinished(
        AssemblyID assemblyId,
        HRESULT hrStatus) noexcept;

    HRESULT ModuleLoadFinished(
        ModuleID moduleId,
        HRESULT hrStatus) noexcept;

    HRESULT ModuleAttachedToAssembly(
        ModuleID moduleId,
        AssemblyID assemblyId) noexcept;

    HRESULT ClassLoadStarted(
        ClassID classId) noexcept;

    HRESULT ClassLoadFinished(
        ClassID classId,
        HRESULT hrStatus) noexcept;

    HRESULT ClassUnloadStarted(
        ClassID classId) noexcept;

    HRESULT ThreadCreated(
        ThreadID threadId) noexcept;

    HRESULT ThreadDestroyed(
        ThreadID threadId) noexcept;

    HRESULT ThreadAssignedToOSThread(
        ThreadID managedThreadId,
        DWORD osThreadId) noexcept;

private:
    int m_tlsThreadInfoIndex;

    SharedResource<ThreadStorage> m_threadStorage;
    SharedResource<ClassStorage>  m_classStorage;

    SigAction m_pauseAction;
    SigAction m_resumeAction;
    SigAction m_sampleAction;

    std::thread m_logThread;
    std::thread m_samplingThread;

    bool m_samplingSuspended;

public:
    auto GetThreadStorage() -> decltype(m_threadStorage.lock());

    auto GetThreadStorage() const -> decltype(m_threadStorage.lock_shared());

    auto GetClassStorage() -> decltype(m_classStorage.lock());

    auto GetClassStorage() const -> decltype(m_classStorage.lock_shared());
};

#endif // _COMMON_TRACE_H_
