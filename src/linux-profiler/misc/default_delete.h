#include <utility>

#include <cor.h>

namespace std
{
    template<>
    struct default_delete<IUnknown> {
        void operator()(IUnknown* pUnknown)
        {
            pUnknown->Release();
        }
    };
}
