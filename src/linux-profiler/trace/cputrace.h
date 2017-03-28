#ifndef _CPU_TRACE_H_
#define _CPU_TRACE_H_

#include <thread>

#include <time.h>

#include <cor.h>
#include <corhdr.h>
#include <corprof.h>

#include "basetrace.h"

#include "threadinfo.h"

class CpuTrace final : public BaseTrace
{
public:
    CpuTrace(Profiler &profiler);

    ~CpuTrace();

    void ProcessConfig(ProfilerConfig &config);

    void Shutdown() noexcept;

private:
    static DWORD64 GetClockTime(clockid_t clk_id);

    void LogProcessTime();

    void LogThreadTime(ThreadInfo &thrInfo);

    void LogThread() noexcept;

private:
    std::thread m_logThread;

    DWORD64     lastUserTime;
};

#endif // _CPU_TRACE_H_
