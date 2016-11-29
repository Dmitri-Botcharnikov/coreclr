#ifndef _MEMORY_TRACE_H_
#define _MEMORY_TRACE_H_

#include <cor.h>
#include <corhdr.h>
#include <corprof.h>

#include "basetrace.h"

class MemoryTrace : public BaseTrace
{
private:
    using SamplingSharedState = CommonTrace::SamplingSharedState;

public:
    MemoryTrace(Profiler &profiler);

    ~MemoryTrace();

    void ProcessConfig(ProfilerConfig &config);

    void Shutdown() noexcept;

    bool NeedSample(
        ThreadInfo &thrInfo, SamplingSharedState &state) const noexcept;

    // void PrepareSample(
    //     ThreadInfo &thrInfo, SamplingSharedState &state) noexcept;

    // void AfterSample(
    //     ThreadInfo &thrInfo, SamplingSharedState &state) noexcept;

private:

public:
    //
    // Events.
    //

    HRESULT ObjectAllocated(
        ObjectID objectId,
        ClassID classId) noexcept;
};

#endif // _MEMORY_TRACE_H_
