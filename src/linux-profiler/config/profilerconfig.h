#ifndef _PROFILER_CONFIG_H_
#define _PROFILER_CONFIG_H_

#include <vector>
#include <string>

//
// The ProfilerConfig structure describes configuration of the Profiler.
//
// NOTE: structure of the configuration can be changed when appropriate logic
// will be implemented.
//
// TODO: configuration parameters should be described after they will be used
// in the implementation.
//

enum class CollectionMethod
{
    None,
    Instrumentation,
    Sampling,
};

struct ProfilerConfig
{
    // Creates configuration with default values.
    ProfilerConfig();

    //
    // Common Trace features.
    //

    CollectionMethod CollectionMethod;
    unsigned long    SamplingTimeoutMs;
    bool             HighGranularityEnabled;
    bool             TracingSuspendedOnStart;
    bool             LineTraceEnabled;

    //
    // CPU Trace features.
    //

    bool             CpuTraceProcessEnabled;
    bool             CpuTraceThreadEnabled;
    unsigned long    CpuTraceTimeoutMs;

    //
    // Execution Trace features.
    //

    bool             ExecutionTraceEnabled;

    //
    // Memory Trace features.
    //

    bool             MemoryTraceEnabled;
    bool             StackTrackingEnabled;

    //
    // Validation and verification.
    //

    void Validate();

    std::vector<std::string> Verify();

    const char *Name();
};

#endif // _PROFILER_CONFIG_H_
