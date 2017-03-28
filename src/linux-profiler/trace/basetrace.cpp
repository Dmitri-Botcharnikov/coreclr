#include "profiler.h"
#include "profilerinfo.h"
#include "basetrace.h"

BaseTrace::BaseTrace(Profiler &profiler)
    : m_disabled(true)
    , m_profiler(profiler)
    , m_info(profiler.GetProfilerInfo())
{
}

BaseTrace::~BaseTrace()
{
}

Log &BaseTrace::LOG() const noexcept
{
    return m_profiler.LOG();
}

ITraceLog &BaseTrace::TRACE() const noexcept
{
    return m_profiler.TRACE();
}

bool BaseTrace::IsEnabled() const noexcept
{
    return !m_disabled;
}
