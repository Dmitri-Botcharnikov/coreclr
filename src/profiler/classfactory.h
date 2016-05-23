#ifndef __PROF_CLASSFACTORY_H__
#define __PROF_CLASSFACTORY_H__

#include <unknwn.h>

// This typedef is for a function which will create a new instance of an object
typedef HRESULT (*PFN_CREATE_OBJ)(REFIID riid, void **ppvObject);

//*****************************************************************************
// This structure is used to declare a global list of coclasses.  The class
// factory object is created with a pointer to the correct one of these, so
// that when create instance is called, it can be created.
//*****************************************************************************
struct COCLASS_REGISTER
{
    const GUID *pClsid;             // Class ID of the coclass
    LPCWSTR    szProgID;            // Prog ID of the class
    PFN_CREATE_OBJ pfnCreateObject; // Creation function to create instance
};

class ProfClassFactory : public IClassFactory
{
public:
    ProfClassFactory(const COCLASS_REGISTER *pCoClass);

    virtual ~ProfClassFactory();

    //
    // IUnknown methods
    //

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        void **ppvObject) override;

    virtual ULONG STDMETHODCALLTYPE AddRef(void) override;

    virtual ULONG STDMETHODCALLTYPE Release(void) override;

    //
    // IClassFactory methods
    //

    virtual HRESULT STDMETHODCALLTYPE CreateInstance(
        IUnknown *pUnkOuter,
        REFIID riid,
        void **ppvObject) override;

    virtual HRESULT STDMETHODCALLTYPE LockServer(
        BOOL fLock) override;

private:
    ProfClassFactory() {}

    LONG m_refCount;                    // Reference count
    const COCLASS_REGISTER *m_pCoClass; // The class we belong to
};

#endif // __PROF_CLASSFACTORY_H__
