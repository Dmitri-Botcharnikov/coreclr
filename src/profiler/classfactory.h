#include <unknwn.h>

class ClassFactory : public IClassFactory
{
public:
    ClassFactory();

    virtual ~ClassFactory();

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        void **ppvObject) override;

    virtual ULONG STDMETHODCALLTYPE AddRef(void) override;

    virtual ULONG STDMETHODCALLTYPE Release(void) override;

    virtual HRESULT STDMETHODCALLTYPE CreateInstance(
        IUnknown *pUnkOuter,
        REFIID riid,
        void **ppvObject) override;

    virtual HRESULT STDMETHODCALLTYPE LockServer(
        BOOL fLock) override;

private:
    LONG m_referenceCount;
};
