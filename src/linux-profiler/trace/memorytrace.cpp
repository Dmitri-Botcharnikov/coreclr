#include "profiler.h"
#include "memorytrace.h"

MemoryTrace::MemoryTrace(Profiler &profiler)
    : BaseTrace(profiler)
{
}

MemoryTrace::~MemoryTrace()
{
    // NOTE: we are dealing with a partially destroyed m_profiler!
    this->Shutdown();
}

void MemoryTrace::ProcessConfig(ProfilerConfig &config)
{
    //
    // Check activation condition.
    //

    if (config.MemoryTraceEnabled)
    {
        m_disabled = false;
    }
    else
    {
        return;
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
            "MemoryTrace::ProcessConfig(): GetEventMask()", hr
        );
    }

    // This events are common for memory tracing.
    events = events
        | COR_PRF_ENABLE_OBJECT_ALLOCATED
        | COR_PRF_MONITOR_OBJECT_ALLOCATED;

    //
    // Set Event Mask.
    //

    hr = m_info.v1()->SetEventMask(events);
    if (FAILED(hr))
    {
        throw HresultException(
            "MemoryTrace::ProcessConfig(): SetEventMask()", hr);
    }
}

void MemoryTrace::Shutdown() noexcept
{
    m_disabled = true;
}

bool MemoryTrace::NeedSample(
    ThreadInfo &thrInfo, SamplingSharedState &state) const noexcept
{
    if (m_disabled)
        return false;

    return thrInfo.eventChannel.HasAllocSample() &&
        (
            (thrInfo.fixTicks != state.genTicks) ||
            (m_profiler.GetConfig().CollectionMethod ==
                CollectionMethod::Instrumentation) ||
            (m_profiler.GetConfig().StackTrackingEnabled &&
                state.stackWillBeChanged)
        );
}

HRESULT MemoryTrace::ObjectAllocated(
    ObjectID objectId,
    ClassID classId) noexcept
{
    if (m_disabled)
        return S_OK;

    if (m_profiler.GetCommonTrace().IsSamplingSuspended())
        return S_OK;

    HRESULT hr = S_OK;
    try
    {
        auto storage_lock = m_profiler.GetCommonTrace().GetClassStorage();
        ClassInfo &classInfo = storage_lock->Place(classId).first;
        classInfo.Initialize(m_profiler, *storage_lock);
        if (!classInfo.isNamePrinted)
        {
            TRACE().DumpClassName(classInfo);
            classInfo.isNamePrinted = true;
        }

        SIZE_T objectSize = 0;
        if (m_info.version() >= 4)
        {
            hr = m_info.v4()->GetObjectSize2(objectId, &objectSize);
        }
        else
        {
            ULONG size = 0;
            hr = m_info.v1()->GetObjectSize(objectId, &size);
            objectSize = size;
        }
        if (FAILED(hr))
        {
            throw HresultException(
                "MemoryTrace::ObjectAllocated(): GetObjectSize()", hr);
        }

        UINT_PTR ip = 0;
        SamplingSharedState state = {};
        m_profiler.GetCommonTrace().InterruptSampling(
            state,
            [this, &classInfo, &objectSize, &ip]
            (ThreadInfo &thrInfo, SamplingSharedState &state)
            {
                EventChannel &channel = thrInfo.eventChannel;
                if (m_profiler.GetConfig().LineTraceEnabled &&
                    channel.GetStackSize() > 0)
                {
                    if (state.isIpRestored)
                    {
                        ip = channel.GetFrameFromTop().ip;
                    }
                    else
                    {
                        ip = m_profiler.GetExecutionTrace().
                            GetCurrentManagedIP(thrInfo);
                    }
                }
                channel.Allocation(classInfo, objectSize, ip);
            }
        );
    }
    catch (const std::exception &e)
    {
        hr = m_profiler.HandleException(e);
    }

    return hr;
}
