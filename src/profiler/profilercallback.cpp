#include "basehlp.h"

#include <corhlpr.h>

#include "profilercallback.h"
#include "log.h"

#define InitializeCriticalSectionAndSpinCount(lpCriticalSection, dwSpinCount) \
    InitializeCriticalSectionEx(lpCriticalSection, dwSpinCount, 0)

EXTERN_C void EnterNaked3(FunctionIDOrClientID functionIDOrClientID);
EXTERN_C void LeaveNaked3(FunctionIDOrClientID functionIDOrClientID);
EXTERN_C void TailcallNaked3(FunctionIDOrClientID functionIDOrClientID);

EXTERN_C void __stdcall EnterStub(FunctionIDOrClientID functionIDOrClientID)
{
    LogProfilerActivity("EnterStub\n");
    ProfilerCallback::Enter(functionIDOrClientID.functionID);
}

EXTERN_C void __stdcall LeaveStub(FunctionIDOrClientID functionIDOrClientID)
{
    LogProfilerActivity("LeaveStub\n");
    ProfilerCallback::Leave(functionIDOrClientID.functionID);
}

EXTERN_C void __stdcall TailcallStub(FunctionIDOrClientID functionIDOrClientID)
{
    LogProfilerActivity("TailcallStub\n");
    ProfilerCallback::Tailcall(functionIDOrClientID.functionID);
}

static bool ContainsHighUnicodeCharsOrQuoteChar(__in_ecount(strLen) WCHAR *str, size_t strLen, WCHAR quoteChar)
{
    for (size_t i = 0; i < strLen; i++)
    {
        WCHAR c = str[i];
        if (c == quoteChar || c == '\\' || c >= 0x100)
            return true;
        if (c == 0)
            break;
    }
    return false;
}

static void InsertEscapeChars(__inout_ecount(strLen) WCHAR *str, size_t strLen, WCHAR quoteChar)
{
    WCHAR quotedName[4*MAX_LENGTH];

    size_t count = 0;
    for (size_t i = 0; i < strLen; i++)
    {
        WCHAR c = str[i];
        if (c == 0)
            break;
        if (c == quoteChar || c == L'\\')
        {
            quotedName[count++] = L'\\';
            quotedName[count++] = c;
        }
        else if (c >= 0x80)
        {
            static WCHAR hexChar[17] = W("0123456789abcdef");

            quotedName[count++] = '\\';
            quotedName[count++] = 'u';
            quotedName[count++] = hexChar[(c >> 12) & 0x0f];
            quotedName[count++] = hexChar[(c >>  8) & 0x0f];
            quotedName[count++] = hexChar[(c >>  4) & 0x0f];
            quotedName[count++] = hexChar[(c >>  0) & 0x0f];
        }
        else
        {
            quotedName[count++] = c;
        }
        if (count >= ARRAY_LEN(quotedName)-7 || count >= strLen - 7)
            break;
    }
    quotedName[count++] = 0;
    wcsncpy_s(str, strLen, quotedName, _TRUNCATE);
}

WCHAR *SanitizeUnicodeName(__inout_ecount(strLen) WCHAR *str, size_t strLen, WCHAR quoteChar)
{
    if (ContainsHighUnicodeCharsOrQuoteChar(str, strLen, quoteChar))
        InsertEscapeChars(str, strLen, quoteChar);

    return str;
}

static char *puthex(__out_ecount(32) char *p, SIZE_T val)
{
    static char hexDig[]  = "0123456789abcdef";

    *p++ = ' ';
    *p++ = '0';
    *p++ = 'x';

    char    digStack[sizeof(val)*2];

    int digCount = 0;
    do
    {
        digStack[digCount++] = hexDig[val % 16];
        val /= 16;
    }
    while (val != 0);

    do
    {
        *p++ = digStack[--digCount];
    }
    while (digCount > 0);

    return p;
}

static char *putdec(__out_ecount(32) char *p, SIZE_T val)
{
    *p++ = ' ';

    char    digStack[sizeof(val)*3];

    int digCount = 0;
    do
    {
        SIZE_T newval = val / 10;
        digStack[digCount++] = (char)(val - newval*10 + '0');
        val = newval;
    }
    while (val != 0);

    do
    {
        *p++ = digStack[--digCount];
    }
    while (digCount > 0);

    return p;
}

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

void ProfilerCallback::_SamplingThread()
{
    while(TRUE)
    {
        Sleep(m_dwDefaultTimeoutMs);
        {
            Synchronize guard( m_criticalSection );
            // Check all threads
            SList<ThreadInfo *, ThreadID> *m_pThreadTable = g_pCallbackObject->m_pThreadTable;
            m_pThreadTable->Reset();
            for (;!m_pThreadTable->AtEnd(); m_pThreadTable->Next())
            {
                ThreadInfo *pThreadInfo = m_pThreadTable->Entry();
                // TEXT_OUTLN("SamplingThreadTicks");
                pThreadInfo->ticks++;
            }
        }
    }
}

static void SamplingThread(ProfilerCallback *pProfiler)
{
	TEXT_OUTLN("SamplingThread");
	pProfiler->_SamplingThread();
}

ProfilerCallback::ProfilerCallback()
    : PrfInfo()
    , m_condemnedGenerationIndex(0)
    , m_refCount(1)
    , m_dwShutdown(0)
    , m_callStackCount(0)
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
    , m_bDumpCompleted(FALSE)
    , m_dwSkipObjects(0)
    , m_dwFramesToPrint(0xFFFFFFFF)
    , m_classToMonitor(NULL)
    , m_bTrackingObjects(FALSE)
    , m_bTrackingCalls(FALSE)
    , m_bIsTrackingStackTrace(FALSE)
    , m_oldFormat(FALSE)
    , m_dwDefaultTimeoutMs(0) /* Simple sampling support */
    , m_lastTickCount(0)
    , m_lastClockTick(0)
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
        PAL_fclose( m_stream );
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
        m_stream = PAL_fopen((m_path != NULL) ? m_path : m_logFileName, "w+");
        hr = ( m_stream == NULL ) ? E_FAIL : S_OK;
        if ( SUCCEEDED( hr ) )
        {
            PAL_setvbuf(m_stream, NULL, _IOFBF, 32768);
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

__forceinline void ProfilerCallback::Enter(FunctionID functionID)
{
    ThreadInfo *pThreadInfo = GetThreadInfo();

    if (pThreadInfo != NULL)
    {
        if ( pThreadInfo->ticks )
           g_pCallbackObject->_LogCallTrace(functionID); // Do it before push !
        pThreadInfo->m_pThreadCallStack->Push(functionID);
    }

    //
    // log tracing info if requested
    //
    if ( g_pCallbackObject->m_dwMode & (DWORD)TRACE )
        g_pCallbackObject->_LogCallTrace(functionID);
}

__forceinline void ProfilerCallback::Leave(FunctionID functionID)
{
    ThreadInfo *pThreadInfo = GetThreadInfo();
    if (pThreadInfo != NULL)
    {
        if ( pThreadInfo->ticks )
            g_pCallbackObject->_LogCallTrace(functionID); // Do it before pop !
        pThreadInfo->m_pThreadCallStack->Pop();
    }
}

__forceinline void ProfilerCallback::Tailcall(FunctionID functionID)
{
    ThreadInfo *pThreadInfo = GetThreadInfo();
    if (pThreadInfo != NULL)
    {
        if ( pThreadInfo->ticks )
            g_pCallbackObject->_LogCallTrace(functionID); // Do it before pop !
        pThreadInfo->m_pThreadCallStack->Pop();
    }
}

__forceinline ThreadInfo *ProfilerCallback::GetThreadInfo()
{
    DWORD lastError = GetLastError();
    ThreadInfo *threadInfo = (ThreadInfo *)TlsGetValue(tlsIndex);
    if (threadInfo != NULL)
    {
        SetLastError(lastError);
        return threadInfo;
    }

    ThreadID threadID = 0;
    HRESULT hr = g_pCallbackObject->m_pProfilerInfo->GetCurrentThreadID(&threadID);
    if (SUCCEEDED(hr))
    {
        threadInfo = g_pCallbackObject->m_pThreadTable->Lookup( threadID );
        if (threadInfo == NULL)
        {
            g_pCallbackObject->AddThread( threadID );
            threadInfo = g_pCallbackObject->m_pThreadTable->Lookup( threadID );
        }
        TlsSetValue(tlsIndex, threadInfo);
    }

    SetLastError(lastError);

    return threadInfo;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::QueryInterface(
    REFIID riid,
    void **ppvObject)
{
    if (riid == IID_IUnknown)
        *ppvObject = static_cast<IUnknown *>(this);
    else if (riid == IID_ICorProfilerCallback)
        *ppvObject = static_cast<ICorProfilerCallback *>(this);
    else if (riid == IID_ICorProfilerCallback2)
        *ppvObject = static_cast<ICorProfilerCallback2 *>(this);
    else if (riid == IID_ICorProfilerCallback3)
        *ppvObject = static_cast<ICorProfilerCallback3 *>(this);
    else
    {
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    reinterpret_cast<IUnknown *>(*ppvObject)->AddRef();

    return S_OK;
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

    hr = m_pProfilerInfo3->SetEnterLeaveFunctionHooks3(
        (FunctionEnter3 *)EnterNaked3,
        (FunctionLeave3 *)LeaveNaked3,
        (FunctionTailcall3 *)TailcallNaked3
    );

    if (FAILED(hr))
    {
        Failure( "ICorProfilerInfo::SetEnterLeaveFunctionHooks() FAILED" );
        return hr;
    }

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

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    try
    {
        ULONG size;
        WCHAR name[2048];
        ModuleID moduleId;
        AppDomainID appDomainId;
        if(SUCCEEDED(m_pProfilerInfo->GetAssemblyInfo(assemblyId, 2048, &size, name, &appDomainId, &moduleId)))
        {
            HRESULT hr = E_FAIL;
            ThreadInfo *pThreadInfo = GetThreadInfo();
            if(pThreadInfo != NULL)
            {
                LogToAny("y %d 0x%p %S\n", pThreadInfo->m_win32ThreadID, assemblyId, name);
            }
        }
    }
    catch ( BaseException *exception )
    {
        exception->ReportFailure();
        delete exception;

        Failure();
    }

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

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    try
    {
        ModuleInfo *pModuleInfo = NULL;


        AddModule( moduleId, m_totalModules );
        pModuleInfo = m_pModuleTable->Lookup( moduleId );

        _ASSERT_( pModuleInfo != NULL );

        SIZE_T stackTraceId = _StackTraceId();

        LogToAny( "m %Id %S 0x%p %Id\n",
                  pModuleInfo->m_internalID,
                  pModuleInfo->m_moduleName,
                  pModuleInfo->m_loadAddress,
                  stackTraceId);

        InterlockedIncrement( &m_totalModules );
    }
    catch ( BaseException *exception )
    {
        exception->ReportFailure();
        delete exception;

        Failure();
    }

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

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    try
    {
        AddFunction( functionId, m_totalFunctions );
        m_totalFunctions++;
    }
    catch ( BaseException *exception )
    {
        exception->ReportFailure();
        delete exception;

        Failure();
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::JITCompilationFinished(
    FunctionID functionId,
    HRESULT hrStatus, BOOL fIsSafeToBlock)
{
    LogProfilerActivity("JITCompilationFinished\n");

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////


    HRESULT hr;
    ULONG size;
    LPCBYTE address;
    FunctionInfo *pFunctionInfo = NULL;


    pFunctionInfo = m_pFunctionTable->Lookup( functionId );

    _ASSERT_( pFunctionInfo != NULL );
    hr = m_pProfilerInfo->GetCodeInfo( functionId, &address, &size );
    if ( FAILED( hr ) )
    {
        address = NULL;
        size = 0;
//      This can actually happen unfortunately due to EE limitations
//      Failure( "ICorProfilerInfo::GetCodeInfo() FAILED" );
    }

    ModuleID moduleID = 0;
    ModuleInfo *pModuleInfo = NULL;

    hr = m_pProfilerInfo->GetFunctionInfo( functionId, NULL, &moduleID, NULL );
    if ( SUCCEEDED( hr ) )
    {
        pModuleInfo = m_pModuleTable->Lookup( moduleID );
    }

    SIZE_T stackTraceId = _StackTraceId();

    LogToAny( "f %Id %S %S 0x%p %ld %Id %Id\n",
                pFunctionInfo->m_internalID,
                SanitizeUnicodeName(pFunctionInfo->m_functionName, ARRAY_LEN(pFunctionInfo->m_functionName), L' '),
                SanitizeUnicodeName(pFunctionInfo->m_functionSig,  ARRAY_LEN(pFunctionInfo->m_functionSig), L'\0'),
                address,
                size,
                pModuleInfo ? pModuleInfo->m_internalID : 0,
                stackTraceId);

    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::JITCachedFunctionSearchStarted(
    FunctionID functionId,
    BOOL *pbUseCachedFunction)
{
    LogProfilerActivity("JITCachedFunctionSearchStarted\n");

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    // use the pre-jitted function
    *pbUseCachedFunction = TRUE;

    try
    {
        AddFunction( functionId, m_totalFunctions );
        m_totalFunctions++;
    }
    catch ( BaseException *exception )
    {
        exception->ReportFailure();
        delete exception;

        Failure();
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::JITCachedFunctionSearchFinished(
    FunctionID functionId,
    COR_PRF_JIT_CACHE result)
{
    LogProfilerActivity("JITCachedFunctionSearchFinished\n");

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////


    if ( result == COR_PRF_CACHED_FUNCTION_FOUND )
    {
        HRESULT hr;
        ULONG size;
        LPCBYTE address;
        FunctionInfo *pFunctionInfo = NULL;


        pFunctionInfo = m_pFunctionTable->Lookup( functionId );

        _ASSERT_( pFunctionInfo != NULL );
        hr = m_pProfilerInfo->GetCodeInfo( functionId, &address, &size );
        if ( FAILED( hr ) )
        {
            address = NULL;
            size = 0;
    //      This can actually happen unfortunately due to EE limitations
    //      Failure( "ICorProfilerInfo::GetCodeInfo() FAILED" );
        }
        ModuleID moduleID = 0;
        ModuleInfo *pModuleInfo = NULL;

        hr = m_pProfilerInfo->GetFunctionInfo( functionId, NULL, &moduleID, NULL );
        if ( SUCCEEDED( hr ) )
        {
            pModuleInfo = m_pModuleTable->Lookup( moduleID );
        }
        SIZE_T stackTraceId = _StackTraceId();

        LogToAny( "f %Id %S %S 0x%p %Id %Id %Id\n",
                    pFunctionInfo->m_internalID,
                    pFunctionInfo->m_functionName,
                    pFunctionInfo->m_functionSig,
                    address,
                    size,
                    pModuleInfo ? pModuleInfo->m_internalID : 0,
                    stackTraceId);
    }

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

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( g_pCallbackObject->m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    try
    {
        AddThread( threadId );
    }
    catch ( BaseException *exception )
    {
        exception->ReportFailure();
        delete exception;

        Failure();
    }



    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ThreadDestroyed(
    ThreadID threadId)
{
    LogProfilerActivity("ThreadDestroyed\n");

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( g_pCallbackObject->m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    try
    {
        RemoveThread( threadId );
        TlsSetValue(tlsIndex, NULL);
    }
    catch ( BaseException *exception )
    {
        exception->ReportFailure();
        delete exception;

        Failure();
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ThreadAssignedToOSThread(
    ThreadID managedThreadId,
    DWORD osThreadId)
{
    LogProfilerActivity("ThreadAssignedToOSThread\n");

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( g_pCallbackObject->m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    if ( managedThreadId != NULL )
    {
        if ( osThreadId != NULL )
        {
            try
            {
                UpdateOSThreadID( managedThreadId, osThreadId );
            }
            catch ( BaseException *exception )
            {
                exception->ReportFailure();
                delete exception;

                Failure();
            }
        }
        else
            Failure( "ProfilerCallback::ThreadAssignedToOSThread() returned NULL OS ThreadID" );
    }
    else
        Failure( "ProfilerCallback::ThreadAssignedToOSThread() returned NULL managed ThreadID" );

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

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    if ( reason == COR_PRF_TRANSITION_RETURN )
    {
        try
        {
            // you need to pop the pseudo function Id from the stack
            UpdateCallStack( functionId, POP );
        }
        catch ( BaseException *exception )
        {
            exception->ReportFailure();
            delete exception;

            Failure();
        }
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ManagedToUnmanagedTransition(
    FunctionID functionId,
    COR_PRF_TRANSITION_REASON reason)
{
    LogProfilerActivity("ManagedToUnmanagedTransition\n");

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    if ( reason == COR_PRF_TRANSITION_CALL )
    {
        try
        {
            // record the start of an unmanaged chain
            UpdateCallStack( NULL, PUSH );
            //
            // log tracing info if requested
            //
            if ( m_dwMode & (DWORD)TRACE )
                _LogCallTrace( NULL );

        }
        catch ( BaseException *exception )
        {
            exception->ReportFailure();
            delete exception;

            Failure();
        }
    }

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

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    //
    // identify if this is the first Object allocated callback
    // after dumping the objects as a result of a Gc and revert the state
    //
    if ( m_bDumpGCInfo == TRUE && m_bDumpCompleted == TRUE )
    {
        // reset
        m_bDumpGCInfo = FALSE;
        m_bDumpCompleted = FALSE;

        // flush the log file so the dump is complete there, too
        PAL_fflush(m_stream);
    }

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

    if ((m_dwMode & OBJECT) == 0)
        return S_OK;

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    if (m_bAttachLoaded && m_bWaitingForTheFirstGC)
        return S_OK;

    for (ULONG i = 0; i < cMovedObjectIDRanges; i++)
    {
#if 1
        char buffer[256];
        char *p = buffer;
        *p++ = 'u';
        p = puthex(p, oldObjectIDRangeStart[i]);
        p = puthex(p, newObjectIDRangeStart[i]);
        p = putdec(p, cObjectIDRangeLength[i]);
        *p++ = '\n';
        PAL_fwrite(buffer, p - buffer, 1, m_stream);
#else
        LogToAny("u 0x%p 0x%p %u\n", oldObjectIDRangeStart[i], newObjectIDRangeStart[i], cObjectIDRangeLength[i]);
#endif
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ObjectAllocated(
    ObjectID objectId,
    ClassID classId)
{
    LogProfilerActivity("ObjectAllocated\n");

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    HRESULT hr = S_OK;

    try
    {
        ULONG mySize = 0;

        ThreadInfo *pThreadInfo = GetThreadInfo();

        if ( pThreadInfo != NULL )
        {
            hr = m_pProfilerInfo->GetObjectSize( objectId, &mySize );
            if ( SUCCEEDED( hr ) )
            {
                if (GetTickCount() - m_lastClockTick >= 1)
                    _LogTickCount();

                SIZE_T stackTraceId = _StackTraceId(classId, mySize);
#if 1
                char buffer[128];
                char *p = buffer;
                if (m_oldFormat)
                {
                    *p++ = 'a';
                }
                else
                {
                    *p++ = '!';
                    p = putdec(p, pThreadInfo->m_win32ThreadID);
                }
                p = puthex(p, objectId);
                p = putdec(p, stackTraceId);
                *p++ = '\n';
                PAL_fwrite(buffer, p - buffer, 1, m_stream);
#else
                if (m_oldFormat)
                {
                    LogToAny( "a 0x%p %Id\n", objectId, stackTraceId );
                }
                else
                {
                    LogToAny("! %Id 0x%p %Id\n", pThreadInfo->m_win32ThreadID, objectId, stackTraceId);
                }
#endif
            }
        }
        else
            Failure( "ERROR: ICorProfilerInfo::GetObjectSize() FAILED" );

        m_totalObjectsAllocated++;
    }
    catch ( BaseException *exception )
    {
        exception->ReportFailure();
        delete exception;

        Failure();
    }

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

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    if (m_bAttachLoaded && m_bWaitingForTheFirstGC)
        return S_OK;

    //
    // dump only in the following cases:
    //      case 1: if the user requested through a ForceGC or
    //      case 2: if you operate in stand-alone mode dump at all times
    //
    if (   (m_bDumpGCInfo == TRUE)
        || ( ( (m_dwMode & DYNOBJECT) == 0 ) && ( (m_dwMode & OBJECT) == 1) ) )
    {
        HRESULT hr = S_OK;
        ClassInfo *pClassInfo = NULL;


        // mark the fact that the callback was received
        m_bDumpCompleted = TRUE;

        // dump all the information properly
        hr = _InsertGCClass( &pClassInfo, classId );
        if ( SUCCEEDED( hr ) )
        {
            //
            // remember the stack trace only if you requested the class
            //
            if ( (m_classToMonitor == NULL) || (PAL_wcsstr( pClassInfo->m_className, m_classToMonitor ) != NULL) )
            {
                ULONG size = 0;

                hr =  m_pProfilerInfo->GetObjectSize( objectId, &size );
                if ( SUCCEEDED( hr ) )
                {
#if 1
                    char    buffer[1024];

                    char *p = buffer;
                    *p++ = 'o';
                    p = puthex(p, objectId);
                    p = putdec(p, pClassInfo->m_internalID);
                    p = putdec(p, size);
                    for(ULONG i = 0; i < cObjectRefs; i++)
                    {
                        p = puthex(p, objectRefIds[i]);
                        if (p - buffer > ARRAY_LEN(buffer) - 32)
                        {
                            PAL_fwrite(buffer, p - buffer, 1, m_stream);
                            p = buffer;
                        }
                    }
                    *p++ = '\n';
                    PAL_fwrite(buffer, p - buffer, 1, m_stream);
#else
                    char refs[MAX_LENGTH];


                    LogToAny( "o 0x%p %Id %d ", objectId, pClassInfo->m_internalID, size );
                    refs[0] = NULL;
                    for( ULONG i=0, index=0; i < objectRefs; i++, index++ )
                    {
                        char objToString[sizeof(objectRefIDs[i])*2+5];


                        sprintf_s( objToString, ARRAY_LEN(objToString), "0x%p ", (void *)objectRefIDs[i] );
                        strcat_s( refs, ARRAY_LEN(refs), objToString );
                        //
                        // buffer overrun control for next iteration
                        // every loop adds 11 chars to the array
                        //
                        if ( ((index+1)*(sizeof(objectRefIDs[i])*2+5)) >= (MAX_LENGTH-1) )
                        {
                            LogToAny( "%s ", refs );
                            refs[0] = NULL;
                            index = 0;
                        }
                    }
                    LogToAny( "%s\n",refs );
#endif
                }
                else
                    Failure( "ERROR: ICorProfilerInfo::GetObjectSize() FAILED" );
            }
        }
        else
            Failure( "ERROR: _InsertGCClass FAILED" );
    }
    else
    {
        // to stop runtime from enumerating
        return E_FAIL;
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::RootReferences(
    ULONG cRootRefs,
    ObjectID rootRefIds[])
{
    LogProfilerActivity("RootReferences\n");

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    if (m_bAttachLoaded && m_bWaitingForTheFirstGC)
        return S_OK;

    //
    // dump only in the following cases:
    //      case 1: if the user requested through a ForceGC or
    //      case 2: if you operate in stand-alone mode dump at all times
    //
    if (   (m_bDumpGCInfo == TRUE)
        || ( ( (m_dwMode & DYNOBJECT) == 0 ) && ( (m_dwMode & OBJECT) == 1) ) )
    {
        char rootsToString[MAX_LENGTH];


        // mark the fact that the callback was received
        m_bDumpCompleted = TRUE;

        // dump all the information properly
        LogToAny( "r " );
        rootsToString[0] = NULL;
        for( ULONG i=0, index=0; i < cRootRefs; i++,index++ )
        {
            char objToString[sizeof(rootRefIds[i])*2+5];


            sprintf_s( objToString, ARRAY_LEN(objToString), "0x%p ", (void *)rootRefIds[i] );
            strcat_s( rootsToString, ARRAY_LEN(rootsToString), objToString );
            //
            // buffer overrun control for next iteration
            // every loop adds 16 chars to the array
            //
            if ( ((index+1)*(sizeof(rootRefIds[i])*2+5)) >= (MAX_LENGTH-1) )
            {
                LogToAny( "%s ", rootsToString );
                rootsToString[0] = NULL;
                index = 0;
            }
        }
        LogToAny( "%s\n",rootsToString );
    }
    else
    {
        // to stop runtime from enumerating
        return E_FAIL;
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::GarbageCollectionStarted(
    int cGenerations,
    BOOL generationCollected[],
    COR_PRF_GC_REASON reason)
{
    LogProfilerActivity("GarbageCollectionStarted\n");

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    if (m_bAttachLoaded && m_bWaitingForTheFirstGC)
        m_bWaitingForTheFirstGC = FALSE;

    int generation = COR_PRF_GC_GEN_1;
    for ( ; generation < COR_PRF_GC_LARGE_OBJECT_HEAP; generation++)
        if (!generationCollected[generation])
            break;
    generation--;

    if (m_condemnedGenerationIndex >= ARRAY_LEN(m_condemnedGeneration))
    {
       _THROW_EXCEPTION( "Logic error!  m_condemnedGenerationIndex is out of legal range." );
    }

    m_condemnedGeneration[m_condemnedGenerationIndex++] = generation;

    _GenerationBounds(TRUE, reason == COR_PRF_GC_INDUCED, generation);

    if (((m_dwMode & OBJECT) == OBJECT) && (GetTickCount() - m_lastClockTick >= 1))
        _LogTickCount();

    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::SurvivingReferences(
    ULONG cSurvivingObjectIDRanges,
    ObjectID objectIDRangeStart[],
    ULONG cObjectIDRangeLength[])
{
    LogProfilerActivity("SurvivingReferences\n");

    if ((m_dwMode & OBJECT) == 0)
        return S_OK;

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    if (m_bAttachLoaded && m_bWaitingForTheFirstGC)
        return S_OK;

    for (ULONG i = 0; i < cSurvivingObjectIDRanges; i++)
    {
#if 1
        char buffer[256];
        char *p = buffer;
        *p++ = 'v';
        p = puthex(p, objectIDRangeStart[i]);
        p = putdec(p, cObjectIDRangeLength[i]);
        *p++ = '\n';
        PAL_fwrite(buffer, p - buffer, 1, m_stream);
#else
        LogToAny("v 0x%p %u\n", objectIDRangeStart[i], cObjectIDRangeLength[i]);
#endif
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::GarbageCollectionFinished(void)
{
    LogProfilerActivity("GarbageCollectionFinished\n");

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    if (m_bAttachLoaded && m_bWaitingForTheFirstGC)
        return S_OK;

    if (m_condemnedGenerationIndex == 0)
    {
       _THROW_EXCEPTION( "Logic error!  m_condemnedGenerationIndex is out of legal range." );
    }

    int collectedGeneration = m_condemnedGeneration[--m_condemnedGenerationIndex];
    _GenerationBounds(FALSE, FALSE, collectedGeneration);

    for (int i = 0 ; i <= collectedGeneration ; i++)
        m_GCcounter[i]++;

    if ((m_dwMode & OBJECT) == OBJECT)
        LogToAny( "g %Id %Id %Id\n", m_GCcounter[COR_PRF_GC_GEN_0], m_GCcounter[COR_PRF_GC_GEN_1], m_GCcounter[COR_PRF_GC_GEN_2]);

    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::FinalizeableObjectQueued(
    DWORD finalizerFlags,
    ObjectID objectId)
{
    LogProfilerActivity("FinalizeableObjectQueued\n");

    if ((m_dwMode & OBJECT) == 0)
        return S_OK;

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    if (m_bAttachLoaded && m_bWaitingForTheFirstGC)
        return S_OK;

    LogToAny("l %u 0x%p\n", finalizerFlags, objectId);

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

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    if (m_bAttachLoaded && m_bWaitingForTheFirstGC)
        return S_OK;

    //
    // dump only in the following cases:
    //      case 1: if the user requested through a ForceGC or
    //      case 2: if you operate in stand-alone mode dump at all times
    //
    if (   (m_bDumpGCInfo == TRUE)
        || ( ( (m_dwMode & DYNOBJECT) == 0 ) && ( (m_dwMode & OBJECT) == 1) ) )
    {
        for (ULONG i = 0; i < cRootRefs; i++)
        {
            if (rootKinds[i] == COR_PRF_GC_ROOT_STACK)
            {
                FunctionInfo *pFunctionInfo = m_pFunctionTable->Lookup( rootIds[i] );
                if ( pFunctionInfo != NULL )
                {
                    LogToAny("e 0x%p %u 0x%x %Iu\n", rootRefIds[i], rootKinds[i], rootFlags[i], pFunctionInfo->m_internalID);
                    continue;
                }
            }
            LogToAny("e 0x%p %u 0x%x 0x%p\n", rootRefIds[i], rootKinds[i], rootFlags[i], rootIds[i]);
        }
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::HandleCreated(
    GCHandleID handleId,
    ObjectID initialObjectId)
{
    LogProfilerActivity("HandleCreated\n");

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    HRESULT hr = E_FAIL;
    DWORD win32ThreadID = 0;
    SIZE_T stackTraceId = 0;

    ThreadInfo *pThreadInfo = GetThreadInfo();

    if ( pThreadInfo != NULL )
    {
        win32ThreadID = pThreadInfo->m_win32ThreadID;
        stackTraceId = _StackTraceId();
    }

    LogToAny( "h %d 0x%p 0x%p %Id\n", win32ThreadID, handleId, initialObjectId, stackTraceId );

    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::HandleDestroyed(
    GCHandleID handleId)
{
    LogProfilerActivity("HandleDestroyed\n");

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    HRESULT hr = E_FAIL;
    ThreadID threadID = NULL;
    DWORD win32ThreadID = 0;
    SIZE_T stackTraceId = 0;

    ThreadInfo *pThreadInfo = GetThreadInfo();

    if ( pThreadInfo != NULL )
    {
        win32ThreadID = pThreadInfo->m_win32ThreadID;
        stackTraceId = _StackTraceId();
    }

    LogToAny( "j %d 0x%p %Id\n", win32ThreadID, handleId, stackTraceId );

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

    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( g_pCallbackObject->m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    if ( functionId != NULL )
    {
        try
        {
            UpdateUnwindStack( &functionId, PUSH );
        }
        catch ( BaseException *exception )
        {
            exception->ReportFailure();
            delete exception;

            Failure();
        }
    }
    else
        Failure( "ProfilerCallback::ExceptionUnwindFunctionEnter returned NULL functionId FAILED" );


    return S_OK;
}

HRESULT STDMETHODCALLTYPE ProfilerCallback::ExceptionUnwindFunctionLeave(void)
{
    LogProfilerActivity("ExceptionUnwindFunctionLeave\n");
    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    FunctionID poppedFunctionID = NULL;

    try
    {
        UpdateUnwindStack( &poppedFunctionID, POP );
        UpdateCallStack( poppedFunctionID, POP );
    }
    catch ( BaseException *exception )
    {
        exception->ReportFailure();
        delete exception;

        Failure();
    }

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

    HRESULT hr;
    if (cbClientData != sizeof(ProfConfig))
    {
        Failure( "ProfilerCallback::InitializeForAttach: Client data bogus!\n" );
        return E_INVALIDARG;
    }

    m_bAttachLoaded = TRUE;
    m_bWaitingForTheFirstGC = TRUE;

    ProfConfig * pProfConfig = (ProfConfig *) pvClientData;
    _ProcessProfConfig(pProfConfig);

    if ( (m_dwEventMask & (~COR_PRF_ALLOWABLE_AFTER_ATTACH)) != 0 )
    {
        Failure( "ProfilerCallback::InitializeForAttach: Unsupported event mode for attach" );
        return E_INVALIDARG;
    }
    hr = pCorProfilerInfoUnk->QueryInterface( IID_ICorProfilerInfo,
                                               (void **)&m_pProfilerInfo );

    if ( SUCCEEDED( hr ) )
    {
        hr = pCorProfilerInfoUnk->QueryInterface( IID_ICorProfilerInfo2,
                                                   (void **)&m_pProfilerInfo2 );
    }

    if ( SUCCEEDED( hr ) )
    {
        hr = pCorProfilerInfoUnk->QueryInterface( IID_ICorProfilerInfo3,
                                                   (void **)&m_pProfilerInfo3 );
    }
    if ( SUCCEEDED( hr ) )
    {
        hr = m_pProfilerInfo->SetEventMask( m_dwEventMask );

        if ( SUCCEEDED( hr ) )
        {
            hr = Init(pProfConfig);
            if (FAILED(hr))
            {
                return hr;
            }

            // hr = _InitializeThreadsAndEvents();
            if ( FAILED( hr ) )
                Failure( "Unable to initialize the threads and handles, No profiling" );
            Sleep(100); // Give the threads a chance to read any signals that are already set.
        }
        else
        {
            Failure( "SetEventMask for Profiler Test FAILED" );
        }
    }
    else
        Failure( "Allocation for Profiler Test FAILED" );

    return hr;
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

SIZE_T ProfilerCallback::_StackTraceId(SIZE_T typeId, SIZE_T typeSize)
{
    ThreadID threadID = NULL;

    ThreadInfo *pThreadInfo = GetThreadInfo();
    if ( pThreadInfo != NULL )
    {
        DWORD count = pThreadInfo->m_pThreadCallStack->Count();
        StackTrace stackTrace(count, pThreadInfo->m_pThreadCallStack->m_Array, typeId, typeSize);
        StackTraceInfo *latestStackTraceInfo = pThreadInfo->m_pLatestStackTraceInfo;
        if(latestStackTraceInfo != NULL && latestStackTraceInfo->Compare(stackTrace) == TRUE)
        {
            return latestStackTraceInfo->m_internalId;
        }

        StackTraceInfo *stackTraceInfo = m_pStackTraceTable->Lookup(stackTrace);
        if (stackTraceInfo != NULL)
        {
            pThreadInfo->m_pLatestStackTraceInfo = stackTraceInfo;
            return stackTraceInfo->m_internalId;
        }

        stackTraceInfo = new StackTraceInfo(++m_callStackCount, count, pThreadInfo->m_pThreadCallStack->m_Array, typeId, typeSize);
        if (stackTraceInfo == NULL)
            _THROW_EXCEPTION( "Allocation for StackTraceInfo FAILED" );

        pThreadInfo->m_pLatestStackTraceInfo = stackTraceInfo;
        m_pStackTraceTable->AddEntry(stackTraceInfo, stackTrace);

        ClassInfo *pClassInfo = NULL;
        if (typeId != 0 && typeSize != 0)
        {
            HRESULT hr = _InsertGCClass( &pClassInfo, typeId );
            if ( !SUCCEEDED( hr ) )
                Failure( "ERROR: _InsertGCClass() FAILED" );
        }

        // used to be `s` before the change of format
        LogToAny("n %d", m_callStackCount);

        int flag = 0;
        if (typeId != 0 && typeSize != 0)
        {
            flag |= 1;
        }

        int i, match = 0;
        if (latestStackTraceInfo != NULL)
        {
            match = min(latestStackTraceInfo->m_count, count);
            for(i = 0; i < match; i++)
            {
                if(latestStackTraceInfo->m_stack[i] != (pThreadInfo->m_pThreadCallStack)->m_Array[i])
                {
                    break;
                }
            }

            flag |= (4 * i);
            flag |= (latestStackTraceInfo->m_typeId != 0 && latestStackTraceInfo->m_typeSize != 0) ? 2 : 0;

            match = i;
        }
        /* */

        LogToAny(" %d", flag);

        if (typeId != 0 && typeSize != 0)
        {
            LogToAny(" %Id %d", pClassInfo->m_internalID, typeSize);
        }

        if (flag >= 4)
        {
            LogToAny(" %Id", latestStackTraceInfo->m_internalId);
        }

        for (DWORD frame = match; frame < count; frame++ )
        {
            SIZE_T stackElement = (pThreadInfo->m_pThreadCallStack)->m_Array[frame];
            FunctionInfo *pFunctionInfo = m_pFunctionTable->Lookup( stackElement );
            if ( pFunctionInfo != NULL )
                LogToAny( " %Id", pFunctionInfo->m_internalID );
            else
                Failure( "ERROR: Function Not Found In Function Table" );
        } // end for loop
        LogToAny("\n");

        return stackTraceInfo->m_internalId;
    }
    else
        Failure( "ERROR: Thread Structure was not found in the thread list" );

    return 0;
}

void ProfilerCallback::_LogTickCount()
{
    DWORD tickCount = GetTickCount();
    if (tickCount != m_lastTickCount)
    {
        m_lastTickCount = tickCount;
        LogToAny("i %u\n", tickCount - m_firstTickCount);
    }
    m_lastClockTick = GetTickCount();
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
    pProfConfig->dwDefaultTimeoutMs = BASEHELPER::FetchEnvironment( OMV_TIMEOUTMS );
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

    // printf("dwDefaultTimeoutMs %d\n", pProfConfig->dwDefaultTimeoutMs);

    if (m_bTrackingCalls && pProfConfig->dwDefaultTimeoutMs != 0 && pProfConfig->dwDefaultTimeoutMs != 0xFFFFFFFF)
    {
        DWORD ourId = 0;
        m_dwDefaultTimeoutMs = pProfConfig->dwDefaultTimeoutMs;
        ::CreateThread( NULL, 0, (LPTHREAD_START_ROUTINE)SamplingThread, (void*)this, THREAD_PRIORITY_NORMAL, &ourId);
    }

    // printf("m_bTrackingObjects = %d  m_bTrackingCalls = %d\n", m_bTrackingObjects, m_bTrackingCalls);
}

void ProfilerCallback::LogToAny( const char *format, ... )
{
    ///////////////////////////////////////////////////////////////////////////
//  Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    va_list args;
    va_start( args, format );
    PAL_vfprintf( m_stream, format, args );
    va_end( args );
}

HRESULT ProfilerCallback::_LogCallTrace( FunctionID functionID )
{
    ///////////////////////////////////////////////////////////////////////////
    Synchronize guard( m_criticalSection );
    ///////////////////////////////////////////////////////////////////////////

    HRESULT hr = E_FAIL;
    ThreadID threadID = NULL;


    ThreadInfo *pThreadInfo = GetThreadInfo();

    if ( pThreadInfo != NULL )
    {
        SIZE_T stackTraceId = _StackTraceId();
#if 1
        char buffer[128];
        char *p = buffer;
        if (m_dwDefaultTimeoutMs)
        {
            DWORD ticks = pThreadInfo->ticks;
            if (!ticks)
                return hr;
            pThreadInfo->ticks = 0;
            *p++ = 'x';
            p = putdec(p, GetTickCount() - m_firstTickCount);
            p = putdec(p, ticks);
        } else {
            *p++ = 'c';
        }
        p = putdec(p, pThreadInfo->m_win32ThreadID);
        p = putdec(p, stackTraceId);
        *p++ = '\n';
        PAL_fwrite(buffer, p - buffer, 1, m_stream);
#else
        LogToAny( "c %d %Id\n", pThreadInfo->m_win32ThreadID, stackTraceId );
#endif
    }
    else
        Failure( "ERROR: Thread Structure was not found in the thread list" );

    return hr;
}

HRESULT ProfilerCallback::_GetNameFromElementType( CorElementType elementType, __out_ecount(buflen) WCHAR *buffer, size_t buflen )
{
    HRESULT hr = S_OK;

    switch ( elementType )
    {
        case ELEMENT_TYPE_BOOLEAN:
             wcscpy_s( buffer, buflen, W("System.Boolean") );
             break;

        case ELEMENT_TYPE_CHAR:
             wcscpy_s( buffer, buflen, W("System.Char") );
             break;

        case ELEMENT_TYPE_I1:
             wcscpy_s( buffer, buflen, W("System.SByte") );
             break;

        case ELEMENT_TYPE_U1:
             wcscpy_s( buffer, buflen, W("System.Byte") );
             break;

        case ELEMENT_TYPE_I2:
             wcscpy_s( buffer, buflen, W("System.Int16") );
             break;

        case ELEMENT_TYPE_U2:
             wcscpy_s( buffer, buflen, W("System.UInt16") );
             break;

        case ELEMENT_TYPE_I4:
             wcscpy_s( buffer, buflen, W("System.Int32") );
             break;

        case ELEMENT_TYPE_U4:
             wcscpy_s( buffer, buflen, W("System.UInt32") );
             break;

        case ELEMENT_TYPE_I8:
             wcscpy_s( buffer, buflen, W("System.Int64") );
             break;

        case ELEMENT_TYPE_U8:
             wcscpy_s( buffer, buflen, W("System.UInt64") );
             break;

        case ELEMENT_TYPE_R4:
             wcscpy_s( buffer, buflen, W("System.Single") );
             break;

        case ELEMENT_TYPE_R8:
             wcscpy_s( buffer, buflen, W("System.Double") );
             break;

        case ELEMENT_TYPE_STRING:
             wcscpy_s( buffer, buflen, W("System.String") );
             break;

        case ELEMENT_TYPE_PTR:
             wcscpy_s( buffer, buflen, W("System.IntPtr") );
             break;

        case ELEMENT_TYPE_VALUETYPE:
             wcscpy_s( buffer, buflen, W("struct") );
             break;

        case ELEMENT_TYPE_CLASS:
             wcscpy_s( buffer, buflen, W("class") );
             break;

        case ELEMENT_TYPE_ARRAY:
             wcscpy_s( buffer, buflen, W("System.Array") );
             break;

        case ELEMENT_TYPE_I:
             wcscpy_s( buffer, buflen, W("int_ptr") );
             break;

        case ELEMENT_TYPE_U:
             wcscpy_s( buffer, buflen, W("unsigned int_ptr") );
             break;

        case ELEMENT_TYPE_OBJECT:
             wcscpy_s( buffer, buflen, W("System.Object") );
             break;

        case ELEMENT_TYPE_SZARRAY:
             wcscpy_s( buffer, buflen, W("System.Array") );
             break;

        case ELEMENT_TYPE_MAX:
        case ELEMENT_TYPE_END:
        case ELEMENT_TYPE_VOID:
        case ELEMENT_TYPE_FNPTR:
        case ELEMENT_TYPE_BYREF:
        case ELEMENT_TYPE_PINNED:
        case ELEMENT_TYPE_SENTINEL:
        case ELEMENT_TYPE_CMOD_OPT:
        case ELEMENT_TYPE_MODIFIER:
        case ELEMENT_TYPE_CMOD_REQD:
        case ELEMENT_TYPE_TYPEDBYREF:
        default:
             wcscpy_s( buffer, buflen, W("<UNKNOWN>") );
             break;
    }

    return hr;
}

bool ProfilerCallback::_ClassHasFinalizeMethod(IMetaDataImport *pMetaDataImport, mdToken classToken, DWORD *pdwAttr)
{
    HRESULT hr = S_OK;
//                      printf("got module metadata\n");
    HCORENUM hEnum = 0;
    mdMethodDef methodDefToken[100];
    ULONG methodDefTokenCount = 0;
    hr = pMetaDataImport->EnumMethodsWithName(&hEnum, classToken, W("Finalize"), methodDefToken, 100, &methodDefTokenCount);
    pMetaDataImport->CloseEnum(hEnum);
    if (SUCCEEDED(hr))
    {
//                              if (methodDefTokenCount > 0)
//                                  printf("found %d finalize methods on %S\n", methodDefTokenCount, (*ppClassInfo)->m_className);
        for (ULONG i = 0; i < methodDefTokenCount; i++)
        {
            mdTypeDef classTypeDef;
            WCHAR   szMethod[MAX_CLASS_NAME];
            ULONG   cchMethod;
            PCCOR_SIGNATURE pvSigBlob;
            ULONG   cbSigBlob;
            ULONG   ulCodeRVA;
            DWORD   dwImplFlags;
            hr = pMetaDataImport->GetMethodProps(methodDefToken[i], &classTypeDef, szMethod, MAX_CLASS_NAME, &cchMethod, pdwAttr,
                                                &pvSigBlob, &cbSigBlob, &ulCodeRVA, &dwImplFlags);

            if (SUCCEEDED(hr) && !IsMdStatic(*pdwAttr) && IsMdVirtual(*pdwAttr))
            {
                hEnum = 0;
                mdParamDef params[100];
                ULONG paramCount = 0;
                hr = pMetaDataImport->EnumParams(&hEnum, methodDefToken[i], params, 100, &paramCount);
                pMetaDataImport->CloseEnum(hEnum);
                if (SUCCEEDED(hr))
                {
                    if (paramCount == 0)
                    {
//                          printf("finalize method #%d on %S has attr = %x  impl flags = %x\n", i, (*ppClassInfo)->m_className, dwAttr, dwImplFlags);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool ProfilerCallback::_ClassOverridesFinalize(IMetaDataImport *pMetaDataImport, mdToken classToken)
{
    DWORD dwAttr = 0;
    return _ClassHasFinalizeMethod(pMetaDataImport, classToken, &dwAttr) && IsMdReuseSlot(dwAttr);
}

bool ProfilerCallback::_ClassReintroducesFinalize(IMetaDataImport *pMetaDataImport, mdToken classToken)
{
    DWORD dwAttr = 0;
    return _ClassHasFinalizeMethod(pMetaDataImport, classToken, &dwAttr) && IsMdNewSlot(dwAttr);
}

bool ProfilerCallback::_ClassIsFinalizable(ModuleID moduleID, mdToken classToken)
{
    IMetaDataImport *pMetaDataImport = NULL;
    HRESULT hr = S_OK;
    hr = m_pProfilerInfo->GetModuleMetaData(moduleID, 0, IID_IMetaDataImport, (IUnknown **)&pMetaDataImport);
    if (SUCCEEDED(hr))
    {
        bool result = false;
        while (true)
        {
            WCHAR   szTypeDef[MAX_CLASS_NAME];
            ULONG   chTypeDef = 0;
            DWORD   dwTypeDefFlags = 0;
            mdToken baseToken = mdTokenNil;
            hr = pMetaDataImport->GetTypeDefProps(classToken, szTypeDef, MAX_CLASS_NAME, &chTypeDef, &dwTypeDefFlags, &baseToken);
            if (hr == S_OK)
            {
                if (IsNilToken(baseToken))
                {
//                  printf("  Class %S has no base class - base token = %u\n", szTypeDef, baseToken);
                    return result;
                }
                if (_ClassOverridesFinalize(pMetaDataImport, classToken))
                {
//                  printf("  Class %S overrides Finalize\n", szTypeDef);
                    result = true;
                }
                else if (_ClassReintroducesFinalize(pMetaDataImport, classToken))
                {
//                  printf("  Class %S reintroduces Finalize\n", szTypeDef);
                    result = false;
                }
            }
            else
            {
//              printf("  _ClassIsFinalizable got an error\n");
                return result;
            }

            if (TypeFromToken(baseToken) == mdtTypeRef)
            {
                WCHAR szName[MAX_CLASS_NAME];
                ULONG chName = 0;
                mdToken resolutionScope = mdTokenNil;
                hr = pMetaDataImport->GetTypeRefProps(baseToken, &resolutionScope, szName, MAX_CLASS_NAME, &chName);
                if (hr == S_OK)
                {
//                  printf("trying to resolve %S\n", szName);
                    IMetaDataImport *pMetaDataImportRef = NULL;
                    hr = pMetaDataImport->ResolveTypeRef(baseToken, IID_IMetaDataImport, (IUnknown **)&pMetaDataImportRef, &baseToken);
                    if (hr == S_OK)
                    {
                        pMetaDataImport->Release();
                        pMetaDataImport = pMetaDataImportRef;
                        classToken = baseToken;
//                      printf("successfully resolved class %S\n", szName);
                    }
                    else
                    {
                        PAL_fprintf(PAL_stdout, "got error trying to resolve %S\n", szName);
                        return result;
                    }
                }
            }
            else
                classToken = baseToken;
        }
        pMetaDataImport->Release();
    }
    else
    {
        printf("  _ClassIsFinalizable got an error\n");
        return false;
    }
}

HRESULT ProfilerCallback::_InsertGCClass( ClassInfo **ppClassInfo, ClassID classID )
{
    HRESULT hr = S_OK;

    *ppClassInfo = m_pClassTable->Lookup( classID );
    if ( *ppClassInfo == NULL )
    {
        *ppClassInfo = new ClassInfo( classID );
        if ( *ppClassInfo != NULL )
        {
            //
            // we have 2 cases
            // case 1: class is an array
            // case 2: class is a real class
            //
            ULONG rank = 0;
            CorElementType elementType;
            ClassID realClassID = NULL;
            WCHAR ranks[MAX_LENGTH];
            bool finalizable = false;


            // case 1
            hr = m_pProfilerInfo->IsArrayClass( classID, &elementType, &realClassID, &rank );
            if ( hr == S_OK )
            {
                ClassID prevClassID;


                _ASSERT_( realClassID != NULL );
                ranks[0] = '\0';
                do
                {
                    prevClassID = realClassID;
                    _snwprintf_s( ranks, ARRAY_LEN(ranks), ARRAY_LEN(ranks)-1, W("%s[]"), ranks);
                    hr = m_pProfilerInfo->IsArrayClass( prevClassID, &elementType, &realClassID, &rank );
                    if ( (hr == S_FALSE) || (FAILED(hr)) || (realClassID == NULL) )
                    {
                        //
                        // before you break set the realClassID to the value that it was before the
                        // last unsuccessful call
                        //
                        realClassID = prevClassID;

                        break;
                    }
                }
                while ( TRUE );

                if ( SUCCEEDED( hr ) )
                {
                    WCHAR className[10 * MAX_LENGTH];


                    className[0] = '\0';
                    if ( realClassID != NULL )
                        hr = GetNameFromClassID( realClassID, className );
                    else
                        hr = _GetNameFromElementType( elementType, className, ARRAY_LEN(className) );


                    if ( SUCCEEDED( hr ) )
                    {
                        const size_t len = ARRAY_LEN((*ppClassInfo)->m_className);
                        _snwprintf_s( (*ppClassInfo)->m_className, len, len-1, W("%s %s"),className, ranks  );
                        (*ppClassInfo)->m_objectsAllocated++;
                        (*ppClassInfo)->m_internalID = m_totalClasses;
                        m_pClassTable->AddEntry( *ppClassInfo, classID );
                        LogToAny( "t %Id %d %S\n",(*ppClassInfo)->m_internalID,
                                                  finalizable,
                                                  SanitizeUnicodeName((*ppClassInfo)->m_className, ARRAY_LEN((*ppClassInfo)->m_className), L'\0'));
                    }
                    else
                        Failure( "ERROR: PrfHelper::GetNameFromClassID() FAILED" );
                }
                else
                    Failure( "ERROR: Looping for Locating the ClassID FAILED" );
            }
            // case 2
            else if ( hr == S_FALSE )
            {
                hr = GetNameFromClassID( classID, (*ppClassInfo)->m_className );
                if ( SUCCEEDED( hr ) )
                {
                    (*ppClassInfo)->m_objectsAllocated++;
                    (*ppClassInfo)->m_internalID = m_totalClasses;
                    m_pClassTable->AddEntry( *ppClassInfo, classID );

                    ModuleID moduleID = 0;
                    mdTypeDef typeDefToken = 0;

                    hr = m_pProfilerInfo->GetClassIDInfo(classID, &moduleID, &typeDefToken);
                    if (SUCCEEDED(hr))
                    {
//                      printf("Class %x has module %x and type def token %x\n", classID, moduleID, typeDefToken);
//                      printf("Checking class %S for finalizability\n", (*ppClassInfo)->m_className);
                        finalizable = _ClassIsFinalizable(moduleID, typeDefToken);
//                      if (finalizable)
//                          printf("Class %S is finalizable\n", (*ppClassInfo)->m_className);
//                      else
//                          printf("Class %S is not finalizable\n", (*ppClassInfo)->m_className);
                    }

                    LogToAny( "t %Id %d %S\n",(*ppClassInfo)->m_internalID,
                                              finalizable,
                                              SanitizeUnicodeName((*ppClassInfo)->m_className, ARRAY_LEN((*ppClassInfo)->m_className), L'\0'));
                }
                else
                    Failure( "ERROR: PrfHelper::GetNameFromClassID() FAILED" );
            }
            else
                Failure( "ERROR: ICorProfilerInfo::IsArrayClass() FAILED" );
        }
        else
            Failure( "ERROR: Allocation for ClassInfo FAILED" );

        InterlockedIncrement( &m_totalClasses );
    }
    else
        (*ppClassInfo)->m_objectsAllocated++;


    return hr;
}

void ProfilerCallback::_GenerationBounds(BOOL beforeGarbageCollection, BOOL inducedGc, int collectedGeneration)
{
    if ((m_dwMode & OBJECT) == 0)
        return;

    // we want the log entry on its own tick
    while (GetTickCount() == m_lastTickCount)
        Sleep(0);
    _LogTickCount();

    ULONG maxObjectRanges = 100;
    ULONG cObjectRanges;
    while (true)
    {
        COR_PRF_GC_GENERATION_RANGE *ranges = new COR_PRF_GC_GENERATION_RANGE[maxObjectRanges];

        cObjectRanges = 0;
        HRESULT hr = m_pProfilerInfo2->GetGenerationBounds(maxObjectRanges,
                                                          &cObjectRanges,
                                                           ranges);
        if (hr != S_OK)
            break;

        if (cObjectRanges <= maxObjectRanges)
        {
            LogToAny("b %u %u %u", beforeGarbageCollection, inducedGc, collectedGeneration);
            for (ULONG i = 0; i < cObjectRanges; i++)
            {
                LogToAny(" 0x%p %Iu %Iu %d", ranges[i].rangeStart, ranges[i].rangeLength, ranges[i].rangeLengthReserved, ranges[i].generation);
            }
            LogToAny("\n");
        }

        delete[] ranges;

        if (cObjectRanges <= maxObjectRanges)
            break;

        maxObjectRanges *= 2;
    }
}
