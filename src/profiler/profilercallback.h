#ifndef __PROFILER_CALLBACK_H__
#define __PROFILER_CALLBACK_H__

// #include <stdlib.h>
// #include <pal_mstypes.h>
// #include <pal.h>
// #include <ntimage.h>
#include <cor.h>
#include <corhdr.h>
#include <corprof.h>

#include "ProfilerInfo.h"

class ProfilerCallback;

extern ProfilerCallback *g_pCallbackObject; // Global reference to callback object

//
// IMPORTANT: ProfConfig structure has a counterpart managed structure defined in
// mainform.cs.  Both must always be in sync.
// Gather all config info in one place. On startup, this may be read from the
// environment. On attach, this is sent as client data.
//
typedef enum _OmvUsage
{
    OmvUsageNone    = 0,
    OmvUsageObjects = 1,
    OmvUsageTrace   = 2,
    OmvUsageBoth    = 3,
    OmvUsageInvalid = 4,
} OmvUsage;

struct ProfConfig
{
    OmvUsage usage;
    BOOL  bOldFormat;
    char  szPath[256];
    char  szFileName[256];
    BOOL  bDynamic;
    BOOL  bStack;
    DWORD dwFramesToPrint;
    DWORD dwSkipObjects;
    char  szClassToMonitor[256];
    DWORD dwInitialSetting;
    DWORD dwDefaultTimeoutMs;
};

class ProfilerCallback
    : public PrfInfo
    , public ICorProfilerCallback3
{
public:
    //
    // Instantiate an instance of the callback interface
    //
    static HRESULT CreateObject(
        REFIID riid,
        void **ppInterface);

    ProfilerCallback();

    virtual ~ProfilerCallback();

    HRESULT Init(ProfConfig * pProfConfig);

    void _SamplingThread();
    void _LogThread();
    void SendToLog(ThreadInfo *);

public:
    // used by function hooks, they have to be static
    static void  Enter( UINT_PTR ptr, char *pc );
    static void  Leave( UINT_PTR ptr );
    static void  Tailcall( UINT_PTR ptr );
    static ThreadInfo *GetThreadInfo();
    void PerfHandler(char *PC);

public:
    //
    // IUnknown methods
    //

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        void **ppvObject) override;

    virtual ULONG STDMETHODCALLTYPE AddRef(void) override;

    virtual ULONG STDMETHODCALLTYPE Release(void) override;

    //
    // STARTUP/SHUTDOWN EVENTS
    //

    virtual HRESULT STDMETHODCALLTYPE Initialize(
        IUnknown *pICorProfilerInfoUnk) override;

    virtual HRESULT STDMETHODCALLTYPE Shutdown(void) override;

    HRESULT DllDetachShutdown();

    //
    // APPLICATION DOMAIN EVENTS
    //

    virtual HRESULT STDMETHODCALLTYPE AppDomainCreationStarted(
        AppDomainID appDomainId) override;

    virtual HRESULT STDMETHODCALLTYPE AppDomainCreationFinished(
        AppDomainID appDomainId,
        HRESULT hrStatus) override;

    virtual HRESULT STDMETHODCALLTYPE AppDomainShutdownStarted(
        AppDomainID appDomainId) override;

    virtual HRESULT STDMETHODCALLTYPE AppDomainShutdownFinished(
        AppDomainID appDomainId,
        HRESULT hrStatus) override;

    //
    // ASSEMBLY EVENTS
    //

    virtual HRESULT STDMETHODCALLTYPE AssemblyLoadStarted(
        AssemblyID assemblyId) override;

    virtual HRESULT STDMETHODCALLTYPE AssemblyLoadFinished(
        AssemblyID assemblyId,
        HRESULT hrStatus) override;

    virtual HRESULT STDMETHODCALLTYPE AssemblyUnloadStarted(
        AssemblyID assemblyId) override;

    virtual HRESULT STDMETHODCALLTYPE AssemblyUnloadFinished(
        AssemblyID assemblyId,
        HRESULT hrStatus) override;

    //
    // MODULE EVENTS
    //

    virtual HRESULT STDMETHODCALLTYPE ModuleLoadStarted(
        ModuleID moduleId) override;

    virtual HRESULT STDMETHODCALLTYPE ModuleLoadFinished(
        ModuleID moduleId,
        HRESULT hrStatus) override;

    virtual HRESULT STDMETHODCALLTYPE ModuleUnloadStarted(
        ModuleID moduleId) override;

    virtual HRESULT STDMETHODCALLTYPE ModuleUnloadFinished(
        ModuleID moduleId,
        HRESULT hrStatus) override;

    virtual HRESULT STDMETHODCALLTYPE ModuleAttachedToAssembly(
        ModuleID moduleId,
        AssemblyID AssemblyId) override;

    //
    // CLASS EVENTS
    //

    virtual HRESULT STDMETHODCALLTYPE ClassLoadStarted(
        ClassID classId) override;

    virtual HRESULT STDMETHODCALLTYPE ClassLoadFinished(
        ClassID classId,
        HRESULT hrStatus) override;

    virtual HRESULT STDMETHODCALLTYPE ClassUnloadStarted(
        ClassID classId) override;

    virtual HRESULT STDMETHODCALLTYPE ClassUnloadFinished(
        ClassID classId,
        HRESULT hrStatus) override;

    virtual HRESULT STDMETHODCALLTYPE FunctionUnloadStarted(
        FunctionID functionId) override;

    //
    // JIT EVENTS
    //

    virtual HRESULT STDMETHODCALLTYPE JITCompilationStarted(
        FunctionID functionId,
        BOOL fIsSafeToBlock) override;

    virtual HRESULT STDMETHODCALLTYPE JITCompilationFinished(
        FunctionID functionId,
        HRESULT hrStatus,
        BOOL fIsSafeToBlock) override;

    virtual HRESULT STDMETHODCALLTYPE JITCachedFunctionSearchStarted(
        FunctionID functionId,
        BOOL *pbUseCachedFunction) override;

    virtual HRESULT STDMETHODCALLTYPE JITCachedFunctionSearchFinished(
        FunctionID functionId,
        COR_PRF_JIT_CACHE result) override;

    virtual HRESULT STDMETHODCALLTYPE JITFunctionPitched(
        FunctionID functionId) override;

    virtual HRESULT STDMETHODCALLTYPE JITInlining(
        FunctionID callerId,
        FunctionID calleeId,
        BOOL *pfShouldInline) override;

    //
    // THREAD EVENTS
    //

    virtual HRESULT STDMETHODCALLTYPE ThreadCreated(
        ThreadID threadId) override;

    virtual HRESULT STDMETHODCALLTYPE ThreadDestroyed(
        ThreadID threadId) override;

    virtual HRESULT STDMETHODCALLTYPE ThreadAssignedToOSThread(
        ThreadID managedThreadId,
        DWORD osThreadId) override;

    virtual HRESULT STDMETHODCALLTYPE ThreadNameChanged(
        ThreadID threadId,
        ULONG cchName,
        _In_reads_opt_(cchName) WCHAR name[]) override;

    //
    // REMOTING EVENTS
    //

    // Client-side events

    virtual HRESULT STDMETHODCALLTYPE RemotingClientInvocationStarted(void) override;

    virtual HRESULT STDMETHODCALLTYPE RemotingClientSendingMessage(
        GUID *pCookie,
        BOOL fIsAsync) override;

    virtual HRESULT STDMETHODCALLTYPE RemotingClientReceivingReply(
        GUID *pCookie,
        BOOL fIsAsync) override;

    virtual HRESULT STDMETHODCALLTYPE RemotingClientInvocationFinished(void) override;

    // Server-side events

    virtual HRESULT STDMETHODCALLTYPE RemotingServerReceivingMessage(
        GUID *pCookie,
        BOOL fIsAsync) override;

    virtual HRESULT STDMETHODCALLTYPE RemotingServerInvocationStarted(void) override;

    virtual HRESULT STDMETHODCALLTYPE RemotingServerInvocationReturned(void) override;

    virtual HRESULT STDMETHODCALLTYPE RemotingServerSendingReply(
        GUID *pCookie,
        BOOL fIsAsync) override;

    //
    // TRANSITION EVENTS
    //

    virtual HRESULT STDMETHODCALLTYPE UnmanagedToManagedTransition(
        FunctionID functionId,
        COR_PRF_TRANSITION_REASON reason) override;

    virtual HRESULT STDMETHODCALLTYPE ManagedToUnmanagedTransition(
        FunctionID functionId,
        COR_PRF_TRANSITION_REASON reason) override;

    //
    // RUNTIME SUSPENSION EVENTS
    //

    virtual HRESULT STDMETHODCALLTYPE RuntimeSuspendStarted(
        COR_PRF_SUSPEND_REASON suspendReason) override;

    virtual HRESULT STDMETHODCALLTYPE RuntimeSuspendFinished(void) override;

    virtual HRESULT STDMETHODCALLTYPE RuntimeSuspendAborted(void) override;

    virtual HRESULT STDMETHODCALLTYPE RuntimeResumeStarted(void) override;

    virtual HRESULT STDMETHODCALLTYPE RuntimeResumeFinished(void) override;

    virtual HRESULT STDMETHODCALLTYPE RuntimeThreadSuspended(
        ThreadID threadId) override;

    virtual HRESULT STDMETHODCALLTYPE RuntimeThreadResumed(
        ThreadID threadId) override;

    //
    // GC EVENTS
    //

    virtual HRESULT STDMETHODCALLTYPE MovedReferences(
        ULONG cMovedObjectIDRanges,
        ObjectID oldObjectIDRangeStart[],
        ObjectID newObjectIDRangeStart[],
        ULONG cObjectIDRangeLength[]) override;

    virtual HRESULT STDMETHODCALLTYPE ObjectAllocated(
        ObjectID objectId,
        ClassID classId) override;

    virtual HRESULT STDMETHODCALLTYPE ObjectsAllocatedByClass(
        ULONG cClassCount,
        ClassID classIds[],
        ULONG cObjects[]) override;

    virtual HRESULT STDMETHODCALLTYPE ObjectReferences(
        ObjectID objectId,
        ClassID classId,
        ULONG cObjectRefs,
        ObjectID objectRefIds[]) override;

    virtual HRESULT STDMETHODCALLTYPE RootReferences(
        ULONG cRootRefs,
        ObjectID rootRefIds[]) override;

    virtual HRESULT STDMETHODCALLTYPE GarbageCollectionStarted(
        int cGenerations,
        BOOL generationCollected[],
        COR_PRF_GC_REASON reason) override;

    virtual HRESULT STDMETHODCALLTYPE SurvivingReferences(
        ULONG cSurvivingObjectIDRanges,
        ObjectID objectIDRangeStart[],
        ULONG cObjectIDRangeLength[]) override;

    virtual HRESULT STDMETHODCALLTYPE GarbageCollectionFinished(void) override;

    virtual HRESULT STDMETHODCALLTYPE FinalizeableObjectQueued(
        DWORD finalizerFlags,
        ObjectID objectId) override;

    virtual HRESULT STDMETHODCALLTYPE RootReferences2(
        ULONG cRootRefs,
        ObjectID rootRefIds[],
        COR_PRF_GC_ROOT_KIND rootKinds[],
        COR_PRF_GC_ROOT_FLAGS rootFlags[],
        UINT_PTR rootIds[]) override;

    virtual HRESULT STDMETHODCALLTYPE HandleCreated(
        GCHandleID handleId,
        ObjectID initialObjectId) override;

    virtual HRESULT STDMETHODCALLTYPE HandleDestroyed(
        GCHandleID handleId) override;

    //
    // EXCEPTION EVENTS
    //

    // Exception creation

    virtual HRESULT STDMETHODCALLTYPE ExceptionThrown(
        ObjectID thrownObjectId) override;

    // Search phase

    virtual HRESULT STDMETHODCALLTYPE ExceptionSearchFunctionEnter(
        FunctionID functionId) override;

    virtual HRESULT STDMETHODCALLTYPE ExceptionSearchFunctionLeave(void) override;

    virtual HRESULT STDMETHODCALLTYPE ExceptionSearchFilterEnter(
        FunctionID functionId) override;

    virtual HRESULT STDMETHODCALLTYPE ExceptionSearchFilterLeave(void) override;

    virtual HRESULT STDMETHODCALLTYPE ExceptionSearchCatcherFound(
        FunctionID functionId) override;

    virtual HRESULT STDMETHODCALLTYPE ExceptionOSHandlerEnter(
        UINT_PTR __unused) override;

    virtual HRESULT STDMETHODCALLTYPE ExceptionOSHandlerLeave(
        UINT_PTR __unused) override;

    // Unwind phase

    virtual HRESULT STDMETHODCALLTYPE ExceptionUnwindFunctionEnter(
        FunctionID functionId) override;

    virtual HRESULT STDMETHODCALLTYPE ExceptionUnwindFunctionLeave(void) override;

    virtual HRESULT STDMETHODCALLTYPE ExceptionUnwindFinallyEnter(
        FunctionID functionId) override;

    virtual HRESULT STDMETHODCALLTYPE ExceptionUnwindFinallyLeave(void) override;

    virtual HRESULT STDMETHODCALLTYPE ExceptionCatcherEnter(
        FunctionID functionId, ObjectID objectId) override;

    virtual HRESULT STDMETHODCALLTYPE ExceptionCatcherLeave(void) override;

    //
    // COM CLASSIC WRAPPER
    //

    virtual HRESULT STDMETHODCALLTYPE COMClassicVTableCreated(
        ClassID wrappedClassId,
        REFGUID implementedIID,
        void *pVTable,
        ULONG cSlots) override;

    virtual HRESULT STDMETHODCALLTYPE COMClassicVTableDestroyed(
        ClassID wrappedClassId,
        REFGUID implementedIID,
        void *pVTable) override;

    //
    // ATTACH EVENTS
    //

    virtual HRESULT STDMETHODCALLTYPE InitializeForAttach(
        IUnknown *pCorProfilerInfoUnk,
        void *pvClientData,
        UINT cbClientData) override;

    virtual HRESULT STDMETHODCALLTYPE ProfilerAttachComplete(void) override;

    virtual HRESULT STDMETHODCALLTYPE ProfilerDetachSucceeded(void) override;

    //
    // DEPRECATED. These callbacks are no longer delivered
    //

    virtual HRESULT STDMETHODCALLTYPE ExceptionCLRCatcherFound(void) override;

    virtual HRESULT STDMETHODCALLTYPE ExceptionCLRCatcherExecute(void) override;

    void LogToAny( const char *format, ... );

private:
    SIZE_T _StackTraceId(SIZE_T typeId=0, SIZE_T typeSize=0);
    void _LogTickCount();
    void _GetProfConfigFromEnvironment(ProfConfig *pProfConfig);
    void _ProcessProfConfig(ProfConfig *pProfConfig);
#ifdef NEW_FRAME_STRUCTURE
    void LogStackChanges(ThreadInfo *pThreadInfo);
#endif /* NEW_FRAME_STRUCTURE */

    HRESULT _LogCallTrace( FunctionID functionID );
    HRESULT _GetNameFromElementType( CorElementType elementType, __out_ecount(buflen) WCHAR *buffer, size_t buflen );
    bool _ClassHasFinalizeMethod(IMetaDataImport *pMetaDataImport, mdToken classToken, DWORD *pdwAttr);
    bool _ClassOverridesFinalize(IMetaDataImport *pMetaDataImport, mdToken classToken);
    bool _ClassReintroducesFinalize(IMetaDataImport *pMetaDataImport, mdToken classToken);
    bool _ClassIsFinalizable(ModuleID moduleID, mdToken classToken);
    HRESULT _InsertGCClass(ClassInfo **ppClassInfo, ClassID classID);
    void _GenerationBounds(BOOL beforeCollection, BOOL induced, int generation);

private:
    ULONG  m_GCcounter[COR_PRF_GC_GEN_2 + 1];
    DWORD  m_condemnedGeneration[2];
    USHORT m_condemnedGenerationIndex;

    // various counters
    LONG m_refCount;
    DWORD m_dwShutdown;
    DWORD m_callStackCount;

    // counters
    LONG m_totalClasses;
    LONG m_totalModules;
    LONG m_totalFunctions;
    ULONG m_totalObjectsAllocated;

    // operation indicators
    char *m_path;
    DWORD m_dwMode;
    BOOL m_bInitialized;
    BOOL m_bShutdown;
    BOOL m_bDumpGCInfo;
    DWORD m_dwProcessId;
    BOOL m_bDumpCompleted;
    DWORD m_dwSkipObjects;
    DWORD m_dwFramesToPrint;
    WCHAR *m_classToMonitor;
    BOOL m_bTrackingObjects;
    BOOL m_bTrackingCalls;
    BOOL m_bIsTrackingStackTrace;
    CRITICAL_SECTION m_criticalSection;
    BOOL m_oldFormat;
    DWORD m_dwDefaultTimeoutMs; // Simple sampling support

    // file stuff
    PAL_FILE *m_stream;
    DWORD m_firstTickCount;
    DWORD m_lastTickCount;
    DWORD m_lastClockTick;
    pthread_t m_logqueue;

    // names for the events and the callbacks
    char m_logFileName[MAX_LENGTH+1];
};

#endif // __PROFILER_CALLBACK_H__
