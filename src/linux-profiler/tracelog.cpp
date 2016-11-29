#include <system_error>
#include <utility>
#include <mutex>

#include <errno.h>

#include <pal.h>

#include "commonconfigconversions.h"
#include "profilerconfigconversions.h"
#include "tracelog.h"

class TraceLog final : public ITraceLog
{
public:
    TraceLog(StdOutStream_t)
        : m_pStream(PAL_stdout)
        , m_bIsOwner(false)
    {}

    TraceLog(StdErrStream_t)
        : m_pStream(PAL_stderr)
        , m_bIsOwner(false)
    {}

    TraceLog(FileStream_t, const std::string &filename)
    {
        m_pStream = PAL_fopen(filename.c_str(), "w");
        if (m_pStream == nullptr)
        {
            throw std::system_error(errno, std::system_category(),
                "can't create TraceLog object");
        }
        m_bIsOwner = true;
    }

    virtual ~TraceLog()
    {
        if (m_bIsOwner)
            PAL_fclose(m_pStream);
    }

    virtual void DumpStartTime(
        const SYSTEMTIME &systime) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(
            m_pStream, "prf stm %04hu-%02hu-%02hu %02hu:%02hu:%02hu.%03hu\n",
            systime.wYear, systime.wMonth, systime.wDay,
            systime.wHour, systime.wMinute, systime.wSecond,
            systime.wMilliseconds
        );
    }

    virtual void DumpProfilerConfig(
        const ProfilerConfig &config) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(
            m_pStream, "prf cfg CollectionMethod %s\n",
            convert<LPCSTR>(config.CollectionMethod)
        );
        PAL_fprintf(
            m_pStream, "prf cfg SamplingTimeoutMs %lu\n",
            config.SamplingTimeoutMs
        );
        PAL_fprintf(
            m_pStream, "prf cfg HighGranularityEnabled %s\n",
            convert<LPCSTR>(config.HighGranularityEnabled)
        );
        PAL_fprintf(
            m_pStream, "prf cfg TracingSuspendedOnStart %s\n",
            convert<LPCSTR>(config.TracingSuspendedOnStart)
        );
        PAL_fprintf(
            m_pStream, "prf cfg LineTraceEnabled %s\n",
            convert<LPCSTR>(config.LineTraceEnabled)
        );
        PAL_fprintf(
            m_pStream, "prf cfg CpuTraceProcessEnabled %s\n",
            convert<LPCSTR>(config.CpuTraceProcessEnabled)
        );
        PAL_fprintf(
            m_pStream, "prf cfg CpuTraceThreadEnabled %s\n",
            convert<LPCSTR>(config.CpuTraceThreadEnabled)
        );
        PAL_fprintf(
            m_pStream, "prf cfg CpuTraceTimeoutMs %lu\n",
            config.CpuTraceTimeoutMs
        );
        PAL_fprintf(
            m_pStream, "prf cfg ExecutionTraceEnabled %s\n",
            convert<LPCSTR>(config.ExecutionTraceEnabled)
        );
        PAL_fprintf(
            m_pStream, "prf cfg MemoryTraceEnabled %s\n",
            convert<LPCSTR>(config.MemoryTraceEnabled)
        );
        PAL_fprintf(
            m_pStream, "prf cfg StackTrackingEnabled %s\n",
            convert<LPCSTR>(config.StackTrackingEnabled)
        );
    }

    virtual void DumpProfilerTracingPause(
        DWORD       ticks) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(m_pStream, "prf tps %d\n", ticks);
    }

    virtual void DumpProfilerTracingResume(
        DWORD       ticks) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(m_pStream, "prf trs %d\n", ticks);
    }

    virtual void DumpProcessTimes(
        DWORD       ticksFromStart,
        DWORD64     userTime) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(m_pStream, "prc cpu %d %I64u\n",
            ticksFromStart, userTime);
    }

    virtual void DumpAppDomainCreationFinished(
        AppDomainID appDomainId,
        LPCWCH      appDomainName,
        ProcessID   processId,
        HRESULT     hrStatus) override
    {
        if (appDomainName == nullptr)
            appDomainName = W("UNKNOWN");

        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(
            m_pStream, "apd crf 0x%p 0x%p 0x%08x \"%S\"\n",
            appDomainId, processId, hrStatus, appDomainName
        );
    }

    virtual void DumpAssemblyLoadFinished(
        AssemblyID  assemblyId,
        LPCWCH      assemblyName,
        AppDomainID appDomainId,
        ModuleID    moduleId,
        HRESULT     hrStatus) override
    {
        if (assemblyName == nullptr)
            assemblyName = W("UNKNOWN");

        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(
            m_pStream, "asm ldf 0x%p 0x%p 0x%p 0x%08x \"%S\"\n",
            assemblyId, appDomainId, moduleId, hrStatus, assemblyName
        );
    }

    virtual void DumpModuleLoadFinished(
        ModuleID    moduleId,
        LPCBYTE     baseLoadAddress,
        LPCWCH      moduleName,
        AssemblyID  assemblyId,
        HRESULT     hrStatus) override
    {
        if (moduleName == nullptr)
            moduleName = W("UNKNOWN");

        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(
            m_pStream, "mod ldf 0x%p 0x%p 0x%p 0x%08x \"%S\"\n",
            moduleId, baseLoadAddress, assemblyId, hrStatus, moduleName
        );
    }

    virtual void DumpModuleAttachedToAssembly(
        ModuleID    moduleId,
        AssemblyID  assemblyId) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(
            m_pStream, "mod ata 0x%p 0x%p\n", moduleId, assemblyId
        );
    }

    virtual void DumpClassLoadFinished(
        const ClassInfo &info,
        HRESULT     hrStatus) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(
            m_pStream, "cls ldf 0x%p 0x%08x 0x%p 0x%08x 0x%08x\n",
            info.id, info.internalId.id, info.moduleId, info.classToken,
            hrStatus
        );
    }

    virtual void DumpClassName(
        const ClassInfo &info) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(
            m_pStream, "cls nam 0x%08x \"%S\"\n",
            info.internalId.id, info.fullName.c_str()
        );
    }

    virtual void DumpJITCompilationFinished(
        const FunctionInfo &info,
        HRESULT     hrStatus) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(
            m_pStream, "fun cmf 0x%p 0x%08x 0x%p 0x%p 0x%08x 0x%08x",
            info.id, info.internalId.id, info.classId, info.moduleId,
            info.funcToken, hrStatus
        );
        DumpFunctionInfo(info);
        PAL_fprintf(m_pStream, "\n");
    }

    virtual void DumpJITCachedFunctionSearchFinished(
        const FunctionInfo &info) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(
            m_pStream, "fun csf 0x%p 0x%08x 0x%p 0x%p 0x%08x",
            info.id, info.internalId.id, info.classId, info.moduleId,
            info.funcToken
        );
        DumpFunctionInfo(info);
        PAL_fprintf(m_pStream, "\n");
    }

    virtual void DumpJITFunctionName(
        const FunctionInfo &info) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(
            m_pStream, "fun nam 0x%08x \"%S\" \"%S\" \"%S\"\n",
            info.internalId.id, info.fullName.c_str(),
            info.returnType.c_str(), info.signature.c_str()
        );
    }

    virtual void DumpThreadCreated(
        ThreadID    threadId,
        InternalID  threadIid) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(m_pStream, "thr crt 0x%p 0x%08x\n", threadId, threadIid.id);
    }

    virtual void DumpThreadDestroyed(
        InternalID  threadIid) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(m_pStream, "thr dst 0x%08x\n", threadIid.id);
    }

    virtual void DumpThreadAssignedToOSThread(
        InternalID  managedThreadIid,
        DWORD       osThreadId) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(
            m_pStream, "thr aos 0x%08x %d\n", managedThreadIid.id, osThreadId
        );
    }

    virtual void DumpThreadTimes(
        InternalID  threadIid,
        DWORD       ticksFromStart,
        DWORD64     userTime) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);
        PAL_fprintf(m_pStream, "thr cpu 0x%08x %d %I64u\n",
            threadIid.id, ticksFromStart, userTime);
    }

    virtual void DumpSample(
        InternalID           threadIid,
        const EventSummary   &summary) override
    {
        std::lock_guard<std::mutex> streamLock(m_mStream);

        if (summary.HasStackSample())
        {
            PAL_fprintf(
                m_pStream, "sam str 0x%08x %d %lu",
                threadIid.id, summary.ticks, summary.count
            );
            PAL_fprintf(m_pStream, " %d:%d",
                summary.matchPrefixSize, summary.stackSize);
            if (summary.ipIsChanged)
            {
                PAL_fprintf(m_pStream, summary.ip != 0 ? ":%p" : ":?", summary.ip);
            }
            for (const auto &frame : summary.newFrames)
            {
                PAL_fprintf(m_pStream, frame.ip != 0 ? " 0x%x:%p" : " 0x%x",
                    frame.pFuncInfo->internalId.id, frame.ip);
            }
            PAL_fprintf(m_pStream, "\n");
        }

        if (summary.HasAllocSample())
        {
            PAL_fprintf(
                m_pStream, "sam mem 0x%08x %d", threadIid.id, summary.ticks
            );
            for (const auto &classIdIpAllocInfo : summary.allocTable)
            {
                for (const auto &IpAllocInfo : classIdIpAllocInfo.second)
                {
                    PAL_fprintf(
                        m_pStream,
                        IpAllocInfo.first != 0 ?
                            " 0x%x:%Iu:%Iu:%p" : " 0x%x:%Iu:%Iu",
                        classIdIpAllocInfo.first,
                        IpAllocInfo.second.allocCount,
                        IpAllocInfo.second.memSize,
                        IpAllocInfo.first
                    );
                }
            }
            PAL_fprintf(m_pStream, "\n");
        }
    }

private:
    PAL_FILE  *m_pStream;
    std::mutex m_mStream;
    bool       m_bIsOwner;

    void DumpFunctionInfo(const FunctionInfo &info)
    {
        for (const auto &ci : info.codeInfo)
        {
            PAL_fprintf(m_pStream, " 0x%p:0x%x",
                ci.startAddress, ci.size);
        }

        for (const auto &m : info.ILToNativeMapping)
        {
            PAL_fprintf(m_pStream, " 0x%x:0x%x:0x%x",
                m.ilOffset, m.nativeStartOffset, m.nativeEndOffset);
        }
    }
};

// static
ITraceLog *ITraceLog::Create(StdOutStream_t StdOutStream)
{
    return new TraceLog(StdOutStream);
}

// static
ITraceLog *ITraceLog::Create(StdErrStream_t StdErrStream)
{
    return new TraceLog(StdErrStream);
}

// static
ITraceLog *ITraceLog::Create(
    FileStream_t FileStream, const std::string &filename)
{
    return new TraceLog(FileStream, filename);
}
