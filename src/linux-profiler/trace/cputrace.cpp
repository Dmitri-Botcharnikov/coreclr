#include <system_error>
#include <exception>

#include <errno.h>

#include "profiler.h"
#include "cputrace.h"

CpuTrace::CpuTrace(Profiler &profiler)
    : BaseTrace(profiler)
    , m_logThread()
    , lastUserTime(0)
{
}

CpuTrace::~CpuTrace()
{
    // NOTE: we are dealing with a partially destroyed m_profiler!
    this->Shutdown();
}

void CpuTrace::ProcessConfig(ProfilerConfig &config)
{
    //
    // Check activation condition.
    //

    if (config.CpuTraceProcessEnabled || config.CpuTraceThreadEnabled)
    {
        m_disabled = false;
    }
    else
    {
        return;
    }

    //
    // Starting service threads.
    //

    m_logThread = std::thread(&CpuTrace::LogThread, this);
}

void CpuTrace::Shutdown() noexcept
{
    m_disabled = true;
    if (m_logThread.joinable())
    {
        m_logThread.join();
    }
}

// static
DWORD64 CpuTrace::GetClockTime(clockid_t clk_id)
{
    struct timespec ts;
    if (clock_gettime(clk_id, &ts))
    {
        throw std::system_error(errno, std::system_category(),
            "CpuTrace::GetClockTime(): clock_gettime()");
    }
    return (DWORD64)ts.tv_sec * 1000000 + ts.tv_nsec / 1000; // (us)
}

void CpuTrace::LogProcessTime()
{
    DWORD64 userTimeUs64 = CpuTrace::GetClockTime(CLOCK_PROCESS_CPUTIME_ID);

    TRACE().DumpProcessTimes(
        m_profiler.GetTickCountFromInit(),
        userTimeUs64 - lastUserTime
    );

    lastUserTime = userTimeUs64;
}

void CpuTrace::LogThreadTime(ThreadInfo &thrInfo)
{
    clockid_t cid;
    int err = pthread_getcpuclockid(thrInfo.nativeHandle, &cid);
    if (err && err != ESRCH)
    {
        throw std::system_error(err, std::system_category(),
            "CpuTrace::LogThreadTimes(): pthread_getcpuclockid()");
    }
    else if (err == ESRCH)
    {
        return;
    }

    DWORD64 userTimeUs64 = CpuTrace::GetClockTime(cid);

    TRACE().DumpThreadTimes(
        thrInfo.internalId,
        m_profiler.GetTickCountFromInit(),
        userTimeUs64 - thrInfo.lastUserTime
    );

    thrInfo.lastUserTime = userTimeUs64;
}

void CpuTrace::LogThread() noexcept
{
    try
    {
        lastUserTime = CpuTrace::GetClockTime(CLOCK_PROCESS_CPUTIME_ID);

        while(m_disabled == false)
        {
            Sleep(m_profiler.GetConfig().CpuTraceTimeoutMs);
            if (m_disabled)
            {
                break;
            }
            else if (m_profiler.GetCommonTrace().IsSamplingSuspended())
            {
                continue;
            }

            if (m_profiler.GetConfig().CpuTraceProcessEnabled)
            {
                this->LogProcessTime();
            }

            if (m_profiler.GetConfig().CpuTraceThreadEnabled)
            {
                auto storage_lock =
                    m_profiler.GetCommonTrace().GetThreadStorage();
                for (ThreadInfo &thrInfo : storage_lock->GetLiveRange())
                {
                    if (m_disabled == true)
                        break;

                    // We update all live threads if they are attached to OS
                    // threads.
                    if (thrInfo.id != 0 && thrInfo.nativeHandle != 0)
                    {
                        this->LogThreadTime(thrInfo);
                    }
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        m_profiler.HandleException(e);
    }
}
