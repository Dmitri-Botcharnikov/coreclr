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
        winContext->R0  = mc->arm_r0;
        winContext->R1  = mc->arm_r1;
        winContext->R2  = mc->arm_r2;
        winContext->R3  = mc->arm_r3;
        winContext->R4  = mc->arm_r4;
        winContext->R5  = mc->arm_r5;
        winContext->R6  = mc->arm_r6;
        winContext->R7  = mc->arm_r7;
        winContext->R8  = mc->arm_r8;
        winContext->R9  = mc->arm_r9;
        winContext->R10 = mc->arm_r10;
        winContext->R11 = mc->arm_fp;
        winContext->R12 = mc->arm_ip;
        winContext->Sp  = mc->arm_sp;
        winContext->Lr  = mc->arm_lr;
        winContext->Pc  = mc->arm_pc;
    }

    return S_OK;
}
