#ifndef _TRACE_LOG_H_
#define _TRACE_LOG_H_

#include <windows.h>

#include <cor.h>
#include <corhdr.h>
#include <corprof.h>

#include "profilerconfig.h"
#include "functioninfo.h"
#include "classinfo.h"
#include "eventchannel.h"

class ITraceLog
{
protected:
    class StdOutStream_t {};

    class StdErrStream_t {};

    class FileStream_t {};

public:
    static StdOutStream_t StdOutStream;

    static StdErrStream_t StdErrStream;

    static FileStream_t   FileStream;

    ITraceLog() = default;

    ITraceLog(const ITraceLog&) = delete;

    ITraceLog &operator=(const ITraceLog&) = delete;

    virtual ~ITraceLog() = default;

    static ITraceLog *Create(StdOutStream_t);

    static ITraceLog *Create(StdErrStream_t);

    static ITraceLog *Create(FileStream_t, const std::string &filename);

    // TODO: different methods to dump information.

    virtual void DumpStartTime(
        const SYSTEMTIME &systime) = 0;

    virtual void DumpProfilerConfig(
        const ProfilerConfig &config) = 0;

    virtual void DumpProfilerTracingPause(
        DWORD       ticks) = 0;

    virtual void DumpProfilerTracingResume(
        DWORD       ticks) = 0;

    virtual void DumpProcessTimes(
        DWORD       ticksFromStart,
        DWORD64     userTime) = 0;

    virtual void DumpAppDomainCreationFinished(
        AppDomainID appDomainId,
        LPCWCH      appDomainName,
        ProcessID   processId,
        HRESULT     hrStatus) = 0;

    virtual void DumpAssemblyLoadFinished(
        AssemblyID  assemblyId,
        LPCWCH      assemblyName,
        AppDomainID appDomainId,
        ModuleID    moduleId,
        HRESULT     hrStatus) = 0;

    virtual void DumpModuleLoadFinished(
        ModuleID    moduleId,
        LPCBYTE     baseLoadAddress,
        LPCWCH      moduleName,
        AssemblyID  assemblyId,
        HRESULT     hrStatus) = 0;

    virtual void DumpModuleAttachedToAssembly(
        ModuleID    moduleId,
        AssemblyID  assemblyId) = 0;

    virtual void DumpClassLoadFinished(
        const ClassInfo &info,
        HRESULT     hrStatus) = 0;

    virtual void DumpClassName(
        const ClassInfo &info) = 0;

    virtual void DumpJITCompilationFinished(
        const FunctionInfo &info,
        HRESULT     hrStatus) = 0;

    virtual void DumpJITCachedFunctionSearchFinished(
        const FunctionInfo &info) = 0;

    virtual void DumpJITFunctionName(
        const FunctionInfo &info) = 0;

    virtual void DumpThreadCreated(
        ThreadID    threadId,
        InternalID  threadIid) = 0;

    virtual void DumpThreadDestroyed(
        InternalID  threadIid) = 0;

    virtual void DumpThreadAssignedToOSThread(
        InternalID  managedThreadIid,
        DWORD       osThreadId) = 0;

    virtual void DumpThreadTimes(
        InternalID  threadIid,
        DWORD       ticksFromStart,
        DWORD64     userTime) = 0;

    virtual void DumpSample(
        InternalID           threadIid,
        const EventSummary   &summary) = 0;
};

#endif // _TRACE_LOG_H_
