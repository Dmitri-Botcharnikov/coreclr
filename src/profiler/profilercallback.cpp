#include <corhlpr.h>

#include "profilercallback.h"
#include "log.h"

ProfilerCallback *g_pCallbackObject; // Global reference to callback object

HRESULT ProfilerCallback::CreateObject(
    REFIID riid,
    void **ppInterface)
{
    HRESULT hr = E_NOINTERFACE;

    *ppInterface = NULL;
    if (   (riid == IID_IUnknown)
        || (riid == IID_ICorProfilerCallback)
        || (riid == IID_ICorProfilerCallback2)
        || (riid == IID_ICorProfilerCallback3))
    {
        ProfilerCallback *pProfiler;

        pProfiler = new (nothrow) ProfilerCallback();
        if (!pProfiler)
            return E_OUTOFMEMORY;

        hr = S_OK;

        pProfiler->AddRef();

        *ppInterface = static_cast<ICorProfilerCallback *>(pProfiler);
    }

    return hr;
}

ProfilerCallback::ProfilerCallback()
    : m_refCount(1)
    , m_dwShutdown(0)
{
}

ProfilerCallback::~ProfilerCallback()
{
}

HRESULT ProfilerCallback::Init(ProfConfig * pProfConfig)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::QueryInterface(
    REFIID riid,
    void **ppvObject)
{
    if (riid == IID_ICorProfilerCallback3 ||
        riid == IID_ICorProfilerCallback2 ||
        riid == IID_ICorProfilerCallback ||
        riid == IID_IUnknown)
    {
        *ppvObject = this;
        this->AddRef();

        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ProfilerCallback::AddRef(void)
{
    return InterlockedIncrement(&m_refCount);
}

ULONG STDMETHODCALLTYPE ProfilerCallback::Release(void)
{
    LONG result = InterlockedDecrement(&m_refCount);
    if (result == 0)
    {
        delete this;
    }

    return result;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::Initialize(
    IUnknown *pICorProfilerInfoUnk)
{
    LogProfilerActivity("Initialize\n");

    ICorProfilerInfo3 *info;
    HRESULT hr = pICorProfilerInfoUnk->QueryInterface(IID_ICorProfilerInfo3, (void **) &info);
    if (hr == S_OK && info != NULL)
    {
        info->SetEventMask(COR_PRF_MONITOR_JIT_COMPILATION | COR_PRF_MONITOR_ASSEMBLY_LOADS | COR_PRF_MONITOR_CLASS_LOADS);

        info->Release();
        info = NULL;
    }

    g_pCallbackObject = this;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::Shutdown(void)
{
    LogProfilerActivity("Shutdown\n");
    m_dwShutdown++;
    return S_OK;
}

HRESULT ProfilerCallback::DllDetachShutdown()
{
    // If no shutdown occurs during DLL_DETACH, release the callback
    // interface pointer. This scenario will more than likely occur
    // with any interop related program (e.g., a program that is
    // comprised of both managed and unmanaged components).
    m_dwShutdown++;
    if ((m_dwShutdown == 1) && (g_pCallbackObject != NULL))
    {
        g_pCallbackObject->Release();
        g_pCallbackObject = NULL;
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::AppDomainCreationStarted(
    AppDomainID appDomainId)
{
    LogProfilerActivity("AppDomainCreationStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::AppDomainCreationFinished(
    AppDomainID appDomainId,
    HRESULT hrStatus)
{
    LogProfilerActivity("AppDomainCreationFinished\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::AppDomainShutdownStarted(
    AppDomainID appDomainId)
{
    LogProfilerActivity("AppDomainShutdownStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::AppDomainShutdownFinished(
    AppDomainID appDomainId,
    HRESULT hrStatus)
{
    LogProfilerActivity("AppDomainShutdownFinished\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::AssemblyLoadStarted(
    AssemblyID assemblyId)
{
    LogProfilerActivity("AssemblyLoadStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::AssemblyLoadFinished(
    AssemblyID assemblyId,
    HRESULT hrStatus)
{
    LogProfilerActivity("AssemblyLoadFinished\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::AssemblyUnloadStarted(
    AssemblyID assemblyId)
{
    LogProfilerActivity("AssemblyUnloadStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::AssemblyUnloadFinished(
    AssemblyID assemblyId,
    HRESULT hrStatus)
{
    LogProfilerActivity("AssemblyUnloadFinished\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ModuleLoadStarted(
    ModuleID moduleId)
{
    LogProfilerActivity("ModuleLoadStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ModuleLoadFinished(
    ModuleID moduleId,
    HRESULT hrStatus)
{
    LogProfilerActivity("ModuleLoadFinished\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ModuleUnloadStarted(
    ModuleID moduleId)
{
    LogProfilerActivity("ModuleUnloadStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ModuleUnloadFinished(
    ModuleID moduleId,
    HRESULT hrStatus)
{
    LogProfilerActivity("ModuleUnloadFinished\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ModuleAttachedToAssembly(
    ModuleID moduleId,
    AssemblyID AssemblyId)
{
    LogProfilerActivity("ModuleAttachedToAssembly\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ClassLoadStarted(
    ClassID classId)
{
    LogProfilerActivity("ClassLoadStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ClassLoadFinished(
    ClassID classId,
    HRESULT hrStatus)
{
    LogProfilerActivity("ClassLoadFinished\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ClassUnloadStarted(
    ClassID classId)
{
    LogProfilerActivity("ClassUnloadStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ClassUnloadFinished(
    ClassID classId,
    HRESULT hrStatus)
{
    LogProfilerActivity("ClassUnloadFinished\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::FunctionUnloadStarted(
    FunctionID functionId)
{
    LogProfilerActivity("FunctionUnloadStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::JITCompilationStarted(
    FunctionID functionId,
    BOOL fIsSafeToBlock)
{
    LogProfilerActivity("JITCompilationStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::JITCompilationFinished(
    FunctionID functionId,
    HRESULT hrStatus, BOOL fIsSafeToBlock)
{
    LogProfilerActivity("JITCompilationFinished\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::JITCachedFunctionSearchStarted(
    FunctionID functionId,
    BOOL *pbUseCachedFunction)
{
    LogProfilerActivity("JITCachedFunctionSearchStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::JITCachedFunctionSearchFinished(
    FunctionID functionId,
    COR_PRF_JIT_CACHE result)
{
    LogProfilerActivity("JITCachedFunctionSearchFinished\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::JITFunctionPitched(
    FunctionID functionId)
{
    LogProfilerActivity("JITFunctionPitched\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::JITInlining(
    FunctionID callerId,
    FunctionID calleeId,
    BOOL *pfShouldInline)
{
    LogProfilerActivity("JITInlining\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ThreadCreated(
    ThreadID threadId)
{
    LogProfilerActivity("ThreadCreated\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ThreadDestroyed(
    ThreadID threadId)
{
    LogProfilerActivity("ThreadDestroyed\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ThreadAssignedToOSThread(
    ThreadID managedThreadId,
    DWORD osThreadId)
{
    LogProfilerActivity("ThreadAssignedToOSThread\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ThreadNameChanged(
    ThreadID threadId,
    ULONG cchName,
    _In_reads_opt_(cchName) WCHAR name[])
{
    LogProfilerActivity("ThreadNameChanged\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RemotingClientInvocationStarted(void)
{
    LogProfilerActivity("RemotingClientInvocationStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RemotingClientSendingMessage(
    GUID *pCookie,
    BOOL fIsAsync)
{
    LogProfilerActivity("RemotingClientSendingMessage\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RemotingClientReceivingReply(
    GUID *pCookie,
    BOOL fIsAsync)
{
    LogProfilerActivity("RemotingClientReceivingReply\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RemotingClientInvocationFinished(void)
{
    LogProfilerActivity("RemotingClientInvocationFinished\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RemotingServerReceivingMessage(
    GUID *pCookie,
    BOOL fIsAsync)
{
    LogProfilerActivity("RemotingServerReceivingMessage\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RemotingServerInvocationStarted(void)
{
    LogProfilerActivity("RemotingServerInvocationStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RemotingServerInvocationReturned(void)
{
    LogProfilerActivity("RemotingServerInvocationReturned\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RemotingServerSendingReply(
    GUID *pCookie,
    BOOL fIsAsync)
{
    LogProfilerActivity("RemotingServerSendingReply\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::UnmanagedToManagedTransition(
    FunctionID functionId,
    COR_PRF_TRANSITION_REASON reason)
{
    LogProfilerActivity("UnmanagedToManagedTransition\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ManagedToUnmanagedTransition(
    FunctionID functionId,
    COR_PRF_TRANSITION_REASON reason)
{
    LogProfilerActivity("ManagedToUnmanagedTransition\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RuntimeSuspendStarted(
    COR_PRF_SUSPEND_REASON suspendReason)
{
    LogProfilerActivity("RuntimeSuspendStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RuntimeSuspendFinished(void)
{
    LogProfilerActivity("RuntimeSuspendFinished\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RuntimeSuspendAborted(void)
{
    LogProfilerActivity("RuntimeSuspendAborted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RuntimeResumeStarted(void)
{
    LogProfilerActivity("RuntimeResumeStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RuntimeResumeFinished(void)
{
    LogProfilerActivity("RuntimeResumeFinished\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RuntimeThreadSuspended(
    ThreadID threadId)
{
    LogProfilerActivity("RuntimeThreadSuspended\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RuntimeThreadResumed(
    ThreadID threadId)
{
    LogProfilerActivity("RuntimeThreadResumed\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::MovedReferences(
    ULONG cMovedObjectIDRanges,
    ObjectID oldObjectIDRangeStart[],
    ObjectID newObjectIDRangeStart[],
    ULONG cObjectIDRangeLength[])
{
    LogProfilerActivity("MovedReferences\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ObjectAllocated(
    ObjectID objectId,
    ClassID classId)
{
    LogProfilerActivity("ObjectAllocated\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ObjectsAllocatedByClass(
    ULONG cClassCount,
    ClassID classIds[],
    ULONG cObjects[])
{
    LogProfilerActivity("ObjectsAllocatedByClass\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ObjectReferences(
    ObjectID objectId,
    ClassID classId,
    ULONG cObjectRefs,
    ObjectID objectRefIds[])
{
    LogProfilerActivity("ObjectReferences\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RootReferences(
    ULONG cRootRefs,
    ObjectID rootRefIds[])
{
    LogProfilerActivity("RootReferences\n");
    return S_OK;
}


HRESULT STDMETHODCALLTYPE ProfilerCallback::GarbageCollectionStarted(
    int cGenerations,
    BOOL generationCollected[],
    COR_PRF_GC_REASON reason)
{
    LogProfilerActivity("GarbageCollectionStarted\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::SurvivingReferences(
    ULONG cSurvivingObjectIDRanges,
    ObjectID objectIDRangeStart[],
    ULONG cObjectIDRangeLength[])
{
    LogProfilerActivity("SurvivingReferences\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::GarbageCollectionFinished(void)
{
    LogProfilerActivity("GarbageCollectionFinished\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::FinalizeableObjectQueued(
    DWORD finalizerFlags,
    ObjectID objectID)
{
    LogProfilerActivity("FinalizeableObjectQueued\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RootReferences2(
    ULONG cRootRefs,
    ObjectID rootRefIds[],
    COR_PRF_GC_ROOT_KIND rootKinds[],
    COR_PRF_GC_ROOT_FLAGS rootFlags[],
    UINT_PTR rootIds[])
{
    LogProfilerActivity("RootReferences2\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::HandleCreated(
    GCHandleID handleId,
    ObjectID initialObjectId)
{
    LogProfilerActivity("HandleCreated\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::HandleDestroyed(
    GCHandleID handleId)
{
    LogProfilerActivity("HandleDestroyed\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionThrown(
    ObjectID thrownObjectId)
{
    LogProfilerActivity("ExceptionThrown\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionSearchFunctionEnter(
    FunctionID functionId)
{
    LogProfilerActivity("ExceptionSearchFunctionEnter\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionSearchFunctionLeave(void)
{
    LogProfilerActivity("ExceptionSearchFunctionLeave\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionSearchFilterEnter(
    FunctionID functionId)
{
    LogProfilerActivity("ExceptionSearchFilterEnter\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionSearchFilterLeave(void)
{
    LogProfilerActivity("ExceptionSearchFilterLeave\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionSearchCatcherFound(
    FunctionID functionId)
{
    LogProfilerActivity("ExceptionSearchCatcherFound\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionOSHandlerEnter(
    UINT_PTR __unused)
{
    LogProfilerActivity("ExceptionOSHandlerEnter\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionOSHandlerLeave(
    UINT_PTR __unused)
{
    LogProfilerActivity("ExceptionOSHandlerLeave\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionUnwindFunctionEnter(
    FunctionID functionId)
{
    LogProfilerActivity("ExceptionUnwindFunctionEnter\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionUnwindFunctionLeave(void)
{
    LogProfilerActivity("ExceptionUnwindFunctionLeave\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionUnwindFinallyEnter(
    FunctionID functionId)
{
    LogProfilerActivity("ExceptionUnwindFinallyEnter\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionUnwindFinallyLeave(void)
{
    LogProfilerActivity("ExceptionUnwindFinallyLeave\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionCatcherEnter(
    FunctionID functionId,
    ObjectID objectId)
{
    LogProfilerActivity("ExceptionCatcherEnter\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionCatcherLeave(void)
{
    LogProfilerActivity("ExceptionCatcherLeave\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::COMClassicVTableCreated(
    ClassID wrappedClassId,
    REFGUID implementedIID,
    void *pVTable, ULONG cSlots)
{
    LogProfilerActivity("COMClassicVTableCreated\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::COMClassicVTableDestroyed(
    ClassID wrappedClassId,
    REFGUID implementedIID,
    void *pVTable)
{
    LogProfilerActivity("COMClassicVTableDestroyed\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::InitializeForAttach(
    IUnknown *pCorProfilerInfoUnk,
    void *pvClientData,
    UINT cbClientData)
{
    LogProfilerActivity("InitializeForAttach\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ProfilerAttachComplete(void)
{
    LogProfilerActivity("ProfilerAttachComplete\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ProfilerDetachSucceeded(void)
{
    LogProfilerActivity("ProfilerDetachSucceeded\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionCLRCatcherFound(void)
{
    LogProfilerActivity("ExceptionCLRCatcherFound\n");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionCLRCatcherExecute(void)
{
    LogProfilerActivity("ExceptionCLRCatcherExecute\n");
    return S_OK;
}
