#include <corhlpr.h>

#include "basehlp.h"

#include "profilercallback.h"
#include "log.h"

#define InitializeCriticalSectionAndSpinCount(lpCriticalSection, dwSpinCount) \
    InitializeCriticalSectionEx(lpCriticalSection, dwSpinCount, 0)

ProfilerCallback *g_pCallbackObject; // Global reference to callback object

int tlsIndex;

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
    : PrfInfo()
    , m_refCount(1)
    , m_dwShutdown(0)
    , m_totalClasses(1)
    , m_totalModules(0)
    , m_totalFunctions(0)
    , m_totalObjectsAllocated(0)
    , m_path(NULL)
    , m_dwMode(0x3)
    , m_bInitialized(FALSE)
    , m_bShutdown(FALSE)
    , m_bDumpGCInfo(FALSE)
    , m_dwProcessId(NULL)
    , m_dwSkipObjects(0)
    , m_dwFramesToPrint(0xFFFFFFFF)
    , m_classToMonitor(NULL)
    , m_bTrackingObjects(FALSE)
    , m_bTrackingCalls(FALSE)
    , m_bIsTrackingStackTrace(FALSE)
    , m_oldFormat(FALSE)
{
}

ProfilerCallback::~ProfilerCallback()
{
    if (!m_bInitialized)
    {
        return;
    }

    if ( m_path != NULL )
    {
        delete[] m_path;
        m_path = NULL;
    }

    if ( m_classToMonitor != NULL )
    {
        delete[] m_classToMonitor;
        m_classToMonitor = NULL;
    }

    if ( m_stream != NULL )
    {
        fclose( m_stream );
        m_stream = NULL;
    }

    DeleteCriticalSection( &m_criticalSection );
    g_pCallbackObject = NULL;
}

HRESULT ProfilerCallback::Init(ProfConfig * pProfConfig)
{
    HRESULT hr = S_OK;
    memset(&m_GCcounter, 0, sizeof(m_GCcounter));
    memset(&m_condemnedGeneration, 0, sizeof(m_condemnedGeneration));

    FunctionInfo *pFunctionInfo = NULL;

    //
    // initializations
    //
    m_firstTickCount = GetTickCount();

    if (!InitializeCriticalSectionAndSpinCount( &m_criticalSection, 10000 ))
        hr = E_FAIL;
    g_pCallbackObject = this;

    //
    // get the processID and connect to the Pipe of the UI
    //
    m_dwProcessId = GetCurrentProcessId();
    sprintf_s( m_logFileName, ARRAY_LEN(m_logFileName), "pipe_%d.log", m_dwProcessId );

    if ( SUCCEEDED(hr) )
    {
        //
        // look if the user specified another path to save the output file
        //
        if ( pProfConfig->szFileName[0] != '\0' )
        {
            // room for buffer chars + '\' + logfilename chars + '\0':
            const size_t len = strlen(pProfConfig->szFileName) + 1;
            m_path = new char[len];
            if ( m_path != NULL )
                strcpy_s( m_path, len, pProfConfig->szFileName );
        }
        else if ( pProfConfig->szPath[0] != '\0' )
        {
            // room for buffer chars + '\' + logfilename chars + '\0':
            const size_t len = strlen(pProfConfig->szPath) + strlen(m_logFileName) + 2;
            m_path = new char[len];
            if ( m_path != NULL )
                sprintf_s( m_path, len, "%s\\%s", pProfConfig->szPath, m_logFileName );
        }

        //
        // open the correct file stream fo dump the logging information
        //
        m_stream = fopen((m_path != NULL) ? m_path : m_logFileName, "w+");
        hr = ( m_stream == NULL ) ? E_FAIL : S_OK;
        if ( SUCCEEDED( hr ) )
        {
            setvbuf(m_stream, NULL, _IOFBF, 32768);
            //
            // add an entry for the stack trace in case of managed to unamanged transitions
            //
            pFunctionInfo = new FunctionInfo( NULL, m_totalFunctions );
            hr = ( pFunctionInfo == NULL ) ? E_FAIL : S_OK;
            if ( SUCCEEDED( hr ) )
            {
                wcscpy_s( pFunctionInfo->m_functionName, ARRAY_LEN(pFunctionInfo->m_functionName), W("NATIVE FUNCTION") );
                wcscpy_s( pFunctionInfo->m_functionSig, ARRAY_LEN(pFunctionInfo->m_functionSig), W("( UNKNOWN ARGUMENTS )") );

                m_pFunctionTable->AddEntry( pFunctionInfo, NULL );
                LogToAny( "f %Id %S %S 0 0\n",
                          pFunctionInfo->m_internalID,
                          pFunctionInfo->m_functionName,
                          pFunctionInfo->m_functionSig );

                m_totalFunctions ++;
            }
            else
                TEXT_OUTLN( "Unable To Allocate Memory For FunctionInfo" )
        }
        else
            TEXT_OUTLN( "Unable to open log file - No log will be produced" )
    }

    tlsIndex = TlsAlloc();
    if (tlsIndex < 0)
        hr = E_FAIL;

    if ( FAILED( hr ) )
        m_dwEventMask = COR_PRF_MONITOR_NONE;

    return hr;
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

    HRESULT hr;
    //
    // define in which mode you will operate
    //
    ProfConfig profConfig;
    _GetProfConfigFromEnvironment(&profConfig);
    _ProcessProfConfig(&profConfig);

    hr = pICorProfilerInfoUnk->QueryInterface(IID_ICorProfilerInfo,
        (void **)&m_pProfilerInfo);
    if (FAILED(hr))
        return hr;

    hr = pICorProfilerInfoUnk->QueryInterface(IID_ICorProfilerInfo2,
        (void **)&m_pProfilerInfo2);
    if (FAILED(hr))
        return hr;

    pICorProfilerInfoUnk->QueryInterface(IID_ICorProfilerInfo3,
        (void **)&m_pProfilerInfo3);

    hr = m_pProfilerInfo->SetEventMask(m_dwEventMask);
    if (FAILED(hr))
    {
        Failure("SetEventMask for Profiler Test FAILED");
        return hr;
    }

    // hr = m_pProfilerInfo3->SetEnterLeaveFunctionHooks3(
    //     (FunctionEnter3 *)EnterNaked3,
    //     (FunctionLeave3 *)LeaveNaked3,
    //     (FunctionTailcall3 *)TailcallNaked3
    // );
    //
    // if (FAILED(hr))
    // {
    //     Failure( "ICorProfilerInfo::SetEnterLeaveFunctionHooks() FAILED" );
    //     return hr;
    // }

    hr = Init(&profConfig);
    if ( FAILED( hr ) )
    {
        Failure( "CLRProfiler initialization FAILED" );
        return hr;
    }

    Sleep(100); // Give the threads a chance to read any signals that are already set.

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

void ProfilerCallback::_GetProfConfigFromEnvironment(ProfConfig * pProfConfig)
{
    char buffer[2*MAX_LENGTH];
    memset(pProfConfig, 0, sizeof(*pProfConfig));

    //
    // read the mode under which the tool is going to operate
    //
    buffer[0] = '\0';
    pProfConfig->usage = OmvUsageInvalid;
    if ( GetEnvironmentVariableA( OMV_USAGE, buffer, MAX_LENGTH ) > 0 )
    {
        if ( _stricmp( "objects", buffer ) == 0 )
        {
            pProfConfig->usage = OmvUsageObjects;
        }
        else if ( _stricmp( "trace", buffer ) == 0 )
        {
            pProfConfig->usage = OmvUsageTrace;
        }
        else if ( _stricmp( "both", buffer ) == 0 )
        {
            pProfConfig->usage = OmvUsageBoth;
        }
        else
        {
            pProfConfig->usage = OmvUsageNone;
        }
    }

    // retrieve the format
    buffer[0] = '\0';
    pProfConfig->bOldFormat = TRUE;
    if ( GetEnvironmentVariableA(OMV_FORMAT, buffer, MAX_LENGTH) > 0 )
    {
        if( _stricmp("v2", buffer) == 0 )
        {
            pProfConfig->bOldFormat = FALSE;
        }
    }

    //
    // look if the user specified another path to save the output file
    //
    if ( GetEnvironmentVariableA( OMV_PATH, pProfConfig->szPath, ARRAY_LEN(pProfConfig->szPath) ) == 0 )
    {
        pProfConfig->szPath[0] = '\0';
    }

    if ( GetEnvironmentVariableA( OMV_FILENAME, pProfConfig->szFileName, ARRAY_LEN(pProfConfig->szFileName) ) == 0 )
    {
        pProfConfig->szFileName[0] = '\0';
    }

    if ( (pProfConfig->usage == OmvUsageObjects) || (pProfConfig->usage == OmvUsageBoth) )
    {
        //
        // check if the user is going to dynamically enable
        // object tracking
        //
        buffer[0] = '\0';
        if ( GetEnvironmentVariableA( OMV_DYNAMIC, buffer, MAX_LENGTH ) > 0 )
        {
            pProfConfig->bDynamic = TRUE;
        }

        //
        // check to see if the user requires stack trace
        //
        DWORD value1 = BASEHELPER::FetchEnvironment( OMV_STACK );

        if ( (value1 != 0x0) && (value1 != 0xFFFFFFFF) )
        {
            pProfConfig->bStack = TRUE;

            //
            // decide how many frames to print
            //
            pProfConfig->dwFramesToPrint = BASEHELPER::FetchEnvironment( OMV_FRAMENUMBER );
        }

        //
        // how many objects you wish to skip
        //
        DWORD dwTemp = BASEHELPER::FetchEnvironment( OMV_SKIP );
        pProfConfig->dwSkipObjects = ( dwTemp != 0xFFFFFFFF ) ? dwTemp : 0;


        //
        // in which class you are interested in
        //
        if ( GetEnvironmentVariableA( OMV_CLASS, pProfConfig->szClassToMonitor, ARRAY_LEN(pProfConfig->szClassToMonitor) ) == 0 )
        {
            pProfConfig->szClassToMonitor[0] = '\0';
        }

    }

    //
    // check to see if there is an inital setting for tracking allocations and calls
    //
    pProfConfig->dwInitialSetting = BASEHELPER::FetchEnvironment( OMV_INITIAL_SETTING );
}

void ProfilerCallback::_ProcessProfConfig(ProfConfig * pProfConfig)
{
    //
    // mask for everything
    //
    m_dwEventMask =  (DWORD) COR_PRF_MONITOR_GC
                   | (DWORD) COR_PRF_MONITOR_THREADS
                   | (DWORD) COR_PRF_MONITOR_SUSPENDS
//                   | (DWORD) COR_PRF_MONITOR_ENTERLEAVE
//                   | (DWORD) COR_PRF_MONITOR_EXCEPTIONS
                   // | (DWORD) COR_PRF_MONITOR_CLASS_LOADS
                   | (DWORD) COR_PRF_MONITOR_MODULE_LOADS
                   | (DWORD) COR_PRF_MONITOR_ASSEMBLY_LOADS
                   | (DWORD) COR_PRF_MONITOR_CACHE_SEARCHES
                   | (DWORD) COR_PRF_ENABLE_OBJECT_ALLOCATED
                   | (DWORD) COR_PRF_MONITOR_JIT_COMPILATION
                   | (DWORD) COR_PRF_MONITOR_OBJECT_ALLOCATED
//                   | (DWORD) COR_PRF_MONITOR_CODE_TRANSITIONS
                    ;

    //
    // read the mode under which the tool is going to operate
    //
    if ( pProfConfig->usage == OmvUsageObjects )
    {
        m_bTrackingObjects = TRUE;
        m_dwMode = (DWORD)OBJECT;
    }
    else if ( pProfConfig->usage == OmvUsageTrace )
    {
        //
        // mask for call graph, remove GC and OBJECT ALLOCATIONS
        //
        m_dwEventMask = m_dwEventMask ^(DWORD) ( COR_PRF_MONITOR_GC
                                               | COR_PRF_MONITOR_OBJECT_ALLOCATED
                                               | COR_PRF_ENABLE_OBJECT_ALLOCATED );
        m_bTrackingCalls = TRUE;
        m_dwMode = (DWORD)TRACE;

    }
    else if ( pProfConfig->usage == OmvUsageBoth )
    {
        m_bTrackingObjects = TRUE;
        m_bTrackingCalls = TRUE;
        m_dwMode = (DWORD)BOTH;
    }
    else
    {
        m_dwEventMask =  (DWORD) COR_PRF_MONITOR_GC
                       | (DWORD) COR_PRF_MONITOR_THREADS
                       | (DWORD) COR_PRF_MONITOR_SUSPENDS;
        m_dwMode = 0;
    }

    // retrieve the format
    m_oldFormat = pProfConfig->bOldFormat;

    if ( m_dwMode & (DWORD)TRACE)
        m_bIsTrackingStackTrace = TRUE;

    //
    // look further for env settings if operating under OBJECT mode
    //
    if ( m_dwMode & (DWORD)OBJECT )
    {
        //
        // check if the user is going to dynamically enable
        // object tracking
        //
        if ( pProfConfig->bDynamic )
        {
            //
            // do not track object when you start up, activate the thread that
            // is going to listen for the event
            //
            m_dwEventMask = m_dwEventMask ^ (DWORD) COR_PRF_MONITOR_OBJECT_ALLOCATED;
            m_bTrackingObjects = FALSE;
            m_dwMode = m_dwMode | (DWORD)DYNOBJECT;
        }


        //
        // check to see if the user requires stack trace
        //
        if ( pProfConfig->bStack )
        {
            m_bIsTrackingStackTrace = TRUE;
            m_dwEventMask = m_dwEventMask
                            | (DWORD) COR_PRF_MONITOR_ENTERLEAVE
                            | (DWORD) COR_PRF_MONITOR_EXCEPTIONS
                            | (DWORD) COR_PRF_MONITOR_CODE_TRANSITIONS;

            //
            // decide how many frames to print
            //
            m_dwFramesToPrint = pProfConfig->dwFramesToPrint;

        }

        //
        // how many objects you wish to skip
        //
        m_dwSkipObjects = pProfConfig->dwSkipObjects;


        //
        // in which class you are interested in
        // if the env variable does not exist copy to it the null
        // string otherwise copy its value
        //
        if ( pProfConfig->szClassToMonitor[0] != '\0')
        {
            const size_t len = strlen(pProfConfig->szClassToMonitor) + 1;
            m_classToMonitor = new WCHAR[len];
            if ( m_classToMonitor != NULL )
            {
                MultiByteToWideChar(CP_ACP, 0, pProfConfig->szClassToMonitor, (int)len, m_classToMonitor, (int)len);
            }
            else
            {
                //
                // some error has happened, do not monitor anything
                //
                printf( "Memory Allocation Error in ProfilerCallback .ctor\n" );
                printf( "**** No Profiling Will Take place **** \n" );
                m_dwEventMask = (DWORD) COR_PRF_MONITOR_NONE;
            }
        }
    }

    //
    // check to see if there is an inital setting for tracking allocations and calls
    //
    if ( pProfConfig->dwInitialSetting != 0xFFFFFFFF )
    {
        if (pProfConfig->dwInitialSetting & 1)
        {
            // Turning object stuff on
            m_dwEventMask = m_dwEventMask | (DWORD) COR_PRF_MONITOR_OBJECT_ALLOCATED;
            m_bTrackingObjects = TRUE;
        }
        else
        {
            // Turning object stuff of
            m_dwEventMask = m_dwEventMask & ~(DWORD) COR_PRF_MONITOR_OBJECT_ALLOCATED;
            m_bTrackingObjects = FALSE;
        }

        if (pProfConfig->dwInitialSetting & 2)
        {
            m_dwMode = m_dwMode | (DWORD)TRACE;
            m_bTrackingCalls = TRUE;
        }
        else
        {
            m_dwMode = m_dwMode & ~(DWORD)TRACE;
            m_bTrackingCalls = FALSE;
        }
    }

//  printf("m_bTrackingObjects = %d  m_bTrackingCalls = %d\n", m_bTrackingObjects, m_bTrackingCalls);
}

void ProfilerCallback::LogToAny( const char *format, ... )
{
    ///////////////////////////////////////////////////////////////////////////
//  Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    va_list args;
    va_start( args, format );
    vfprintf( m_stream, format, args );
}
