#include <assert.h>

#include "commonconfig.h"
#include "profilerconfig.h"

//
// Configuration parameters should be assigned to its defaults here.
//
ProfilerConfig::ProfilerConfig()
    : CollectionMethod(CollectionMethod::None)
    , SamplingTimeoutMs(10)
    , HighGranularityEnabled(true)
    , TracingSuspendedOnStart(false)
    , LineTraceEnabled(false)
    , CpuTraceProcessEnabled(false)
    , CpuTraceThreadEnabled(false)
    , CpuTraceTimeoutMs(10)
    , ExecutionTraceEnabled(false)
    , MemoryTraceEnabled(false)
    , StackTrackingEnabled(true)
{}

void ProfilerConfig::Validate()
{
    if (CollectionMethod == CollectionMethod::Sampling)
    {
        if (SamplingTimeoutMs == 0)
        {
            throw config_error("sampling timeout should be non-zero");
        }
    }

    if (CpuTraceProcessEnabled || CpuTraceThreadEnabled)
    {
        if (CpuTraceTimeoutMs == 0)
        {
            throw config_error("CPU tracing timeout should be non-zero");
        }
    }
}

std::vector<std::string> ProfilerConfig::Verify()
{
    std::vector<std::string> warnings;

    if (CollectionMethod != CollectionMethod::Instrumentation)
    {
        // Instrumentation specific options verification.
    }
    else
    {
        if (StackTrackingEnabled)
        {
            warnings.push_back(
                "stack tracking option is redundant for instrumentation");
        }
    }

    if (CollectionMethod != CollectionMethod::Sampling)
    {
        // Sampling specific options verification.

        if (SamplingTimeoutMs != 0)
        {
            warnings.push_back(
                "sampling timeout specification requires sampling");
        }

        if (HighGranularityEnabled)
        {
            // We don't show this message if sampling have been required for
            // line tracing above.
            warnings.push_back("hight granularity option requires sampling");
        }
    }

    if (CollectionMethod == CollectionMethod::None)
    {
        // Common options verification.

        if (LineTraceEnabled)
        {
            warnings.push_back(
                "line tracing requires sampling or instrumentation");
        }
    }

    if (!CpuTraceProcessEnabled && !CpuTraceThreadEnabled)
    {
        // CPU Trace specific options verification.

        if (CpuTraceTimeoutMs != 0)
        {
            warnings.push_back(
                "CPU tracing timeout specified when tracing disabled");
        }
    }

    if (!ExecutionTraceEnabled && !MemoryTraceEnabled)
    {
        // When all traces are disabled.

        if (CollectionMethod != CollectionMethod::None)
        {
            warnings.push_back(
                "collection method specification requires execution or "
                "memory tracing");
        }

        if (LineTraceEnabled)
        {
            warnings.push_back(
                "line tracing requires execution or memory tracing");
        }
    }

    if (!MemoryTraceEnabled)
    {
        if (StackTrackingEnabled)
        {
            warnings.push_back("stack tracking is memory tracing option");
        }
    }

    return warnings;
}

const char *ProfilerConfig::Name()
{
    return "Profiler configuration";
}
