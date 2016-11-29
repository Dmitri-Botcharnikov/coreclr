#ifndef _BASE_TRACE_H_
#define _BASE_TRACE_H_

class Profiler;

class ProfilerInfo;

class Log;

class ITraceLog;

class BaseTrace
{
protected:
    BaseTrace(Profiler &profiler);

    ~BaseTrace();

    Log &LOG() const noexcept;

    ITraceLog &TRACE() const noexcept;

public:
    bool IsEnabled() const noexcept;

protected:
    bool m_disabled;

    Profiler &m_profiler;
    const ProfilerInfo &m_info;
};

#endif // _BASE_TRACE_H_
