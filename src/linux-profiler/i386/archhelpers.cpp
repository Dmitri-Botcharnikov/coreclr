#include <sys/ucontext.h>

#include <cor.h>
#include <corhdr.h>
#include <corprof.h>

HRESULT ContextToStackSnapshotContext(
    const void *context, CONTEXT *winContext) noexcept
{
    _ASSERTE(context != nullptr && winContext != nullptr);

    *winContext = {CONTEXT_INTEGER};
    const mcontext_t *mc =
        &(reinterpret_cast<const ucontext_t*>(context))->uc_mcontext;

    {
        winContext->Eax  = mc->gregs[REG_EAX];
        winContext->Ebx  = mc->gregs[REG_EBX];
        winContext->Ecx  = mc->gregs[REG_ECX];
        winContext->Edx  = mc->gregs[REG_EDX];
        winContext->Esi  = mc->gregs[REG_ESI];
        winContext->Edi  = mc->gregs[REG_EDI];
        winContext->Ebp  = mc->gregs[REG_EBP];
        winContext->Esp  = mc->gregs[REG_ESP];
        winContext->SegSs  = mc->gregs[REG_SS];
        winContext->EFlags  = mc->gregs[REG_EFL];
        winContext->Eip = mc->gregs[REG_EIP];
        winContext->SegCs = mc->gregs[REG_CS];
    }

    return S_OK;
}
