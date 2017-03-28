#include <stdexcept>
#include <sstream>

#include <stdlib.h>

#include "commonconfig.h"
#include "commonconfigconversions.h"
#include "loggerconfigconversions.h"
#include "tracelogconfigconversions.h"
#include "profilerconfigconversions.h"
#include "environmentconfigprovider.h"

template<typename T>
bool EnvironmentConfigProvider::FetchValue(const char *name, T &value) const
{
    const char *env = getenv(name);
    if (env)
    {
        try
        {
            value = convert<T>(env);
        }
        catch (const std::runtime_error &e)
        {
            std::stringstream ss;
            ss << "variable " << name << "=" << env <<
                " can't be parsed: " << e.what();
            throw config_error(ss.str());
        }

        return true;
    }
    else
    {
        return false;
    }
}

void EnvironmentConfigProvider::FetchConfig(LoggerConfig &config) const
{
    // Save current configuration to temporary.
    LoggerConfig new_config(config);

    //
    // Fetching from the environment can cause exceptions.
    //

    FetchValue("PROF_LOG_LEVEL", new_config.Level);

    bool file_name_specified =
        FetchValue("PROF_LOG_FILENAME", new_config.FileName);
    if (file_name_specified)
    {
        new_config.OutputStream = LoggerOutputStream::File;
    }

    FetchValue("PROF_LOG_STREAM", new_config.OutputStream);

    // Apply changes to the current configuration.
    config = new_config;
}

void EnvironmentConfigProvider::FetchConfig(TraceLogConfig &config) const
{
    // Save current configuration to temporary.
    TraceLogConfig new_config(config);

    //
    // Fetching from the environment can cause exceptions.
    //

    bool file_name_specified =
        FetchValue("PROF_TRACE_FILENAME", new_config.FileName);
    if (file_name_specified)
    {
        new_config.OutputStream = TraceLogOutputStream::File;
    }

    FetchValue("PROF_TRACE_STREAM", new_config.OutputStream);

    // Apply changes to the current configuration.
    config = new_config;
}

void EnvironmentConfigProvider::FetchConfig(ProfilerConfig &config) const
{
    // Save current configuration to temporary.
    ProfilerConfig new_config(config);

    //
    // Fetching from the environment can cause exceptions.
    //
    // We don't check whether the environment variables override the
    // configuration or not.
    //

    FetchValue("PROF_COLLECT_METHOD", new_config.CollectionMethod);
    if (new_config.CollectionMethod != CollectionMethod::Sampling)
    {
        new_config.SamplingTimeoutMs = 0;
        new_config.HighGranularityEnabled = false;
    }
    if (FetchValue("PROF_SAMPLING_TIMEOUT", new_config.SamplingTimeoutMs))
    {
        new_config.CpuTraceTimeoutMs = new_config.SamplingTimeoutMs;
    }
    FetchValue("PROF_HIGH_GRAN",     new_config.HighGranularityEnabled);
    FetchValue("PROF_DELAYED_START", new_config.TracingSuspendedOnStart);
    FetchValue("PROF_LINE_TRACE",    new_config.LineTraceEnabled);

    bool CpuTraceEnabled;
    if (FetchValue("PROF_CPU_TRACE", CpuTraceEnabled))
    {
        new_config.CpuTraceProcessEnabled = CpuTraceEnabled;
        new_config.CpuTraceThreadEnabled  = CpuTraceEnabled;
    }
    FetchValue("PROF_CPU_TRACE_PROC",   new_config.CpuTraceProcessEnabled);
    FetchValue("PROF_CPU_TRACE_THREAD", new_config.CpuTraceThreadEnabled);
    if (!new_config.CpuTraceProcessEnabled && !new_config.CpuTraceThreadEnabled)
    {
        new_config.CpuTraceTimeoutMs = 0;
    }
    FetchValue("PROF_CPU_TRACE_TIMEOUT", new_config.CpuTraceTimeoutMs);

    FetchValue("PROF_EXECUTION_TRACE",  new_config.ExecutionTraceEnabled);
    FetchValue("PROF_MEMORY_TRACE",     new_config.MemoryTraceEnabled);
    if (!new_config.MemoryTraceEnabled)
    {
        new_config.StackTrackingEnabled = false;
    }

    FetchValue("PROF_STACK_TRACK",      new_config.StackTrackingEnabled);

    // Apply changes to the current configuration.
    config = new_config;
}
