#include "classfactory.h"

ProfClassFactory::ProfClassFactory(const COCLASS_REGISTER *pCoClass)
    : m_refCount(1)
    , m_pCoClass(pCoClass)
{
}

ProfClassFactory::~ProfClassFactory()
{
}

HRESULT STDMETHODCALLTYPE ProfClassFactory::QueryInterface(
    REFIID riid,
    void **ppvObject)
{
    // Pick the right v-table based on the IID passed in
    if (riid == IID_IUnknown)
        *ppvObject = (IUnknown *) this;
    else if (riid == IID_IClassFactory)
        *ppvObject = (IClassFactory *) this;
    else
    {
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    // If successful, add a reference for out pointer and return
    AddRef();

    return S_OK;
}

ULONG STDMETHODCALLTYPE ProfClassFactory::AddRef(void)
{
    return InterlockedIncrement(&m_refCount);
}

ULONG STDMETHODCALLTYPE ProfClassFactory::Release(void)
{
    LONG refCount = InterlockedIncrement(&m_refCount);
    if (refCount == 0)
    {
        delete this;
    }

    return refCount;
}

HRESULT STDMETHODCALLTYPE ProfClassFactory::CreateInstance(
    IUnknown *pUnkOuter,
    REFIID riid,
    void **ppvObject)
{
    // Aggregation is not supported by these objects
    if (pUnkOuter != NULL)
        return CLASS_E_NOAGGREGATION;

    // Ask the object to create an instance of itself, and check the iid
    return (*m_pCoClass->pfnCreateObject)(riid, ppvObject);
}

HRESULT STDMETHODCALLTYPE ProfClassFactory::LockServer(BOOL fLock)
{
    return S_OK;
}
