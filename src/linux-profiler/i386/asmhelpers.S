.intel_syntax noprefix
#include "unixasmmacros.inc"

//
// EXTERN_C void EnterNaked3(FunctionIDOrClientID functionIDOrClientID);
//
NESTED_ENTRY EnterNaked3, _TEXT, NoHandler
    push ebp
    mov  ebp, esp
    pushad
    sub  esp, 16*8 /* xmm0-xmm7 */
    and  esp, (-16) /* must be alligned */
    movdqa	[esp+0x00], xmm0
    movdqa	[esp+0x10], xmm1
    movdqa	[esp+0x20], xmm2
    movdqa	[esp+0x30], xmm3
    movdqa	[esp+0x40], xmm4
    movdqa	[esp+0x50], xmm5
    movdqa	[esp+0x60], xmm6
    movdqa	[esp+0x70], xmm7
    call    EXTERNAL_C_FUNC(EnterStub)
    movdqa	xmm0, [esp+0x00]
    movdqa	xmm1, [esp+0x10]
    movdqa	xmm2, [esp+0x20]
    movdqa	xmm3, [esp+0x30]
    movdqa	xmm4, [esp+0x40]
    movdqa	xmm5, [esp+0x50]
    movdqa	xmm6, [esp+0x60]
    movdqa	xmm7, [esp+0x70]
    lea  esp, [ebp - (14 * 8)]
    popad
    pop  ebp
    ret
NESTED_END EnterNaked3, _TEXT

//
// EXTERN_C void LeaveNaked3(FunctionIDOrClientID functionIDOrClientID);
//
NESTED_ENTRY LeaveNaked3, _TEXT, NoHandler
    push ebp
    mov  ebp, esp
    pushad
    sub  esp, 16*8 /* xmm0-xmm7 */
    and  esp, (-16) /* must be alligned */
    movdqa	[esp+0x00], xmm0
    movdqa	[esp+0x10], xmm1
    movdqa	[esp+0x20], xmm2
    movdqa	[esp+0x30], xmm3
    movdqa	[esp+0x40], xmm4
    movdqa	[esp+0x50], xmm5
    movdqa	[esp+0x60], xmm6
    movdqa	[esp+0x70], xmm7
    call    EXTERNAL_C_FUNC(LeaveStub)
    movdqa	xmm0, [esp+0x00]
    movdqa	xmm1, [esp+0x10]
    movdqa	xmm2, [esp+0x20]
    movdqa	xmm3, [esp+0x30]
    movdqa	xmm4, [esp+0x40]
    movdqa	xmm5, [esp+0x50]
    movdqa	xmm6, [esp+0x60]
    movdqa	xmm7, [esp+0x70]
    lea  esp, [ebp - (14 * 8)]
    popad
    pop  ebp
    ret
NESTED_END LeaveNaked3, _TEXT

//
// EXTERN_C void TailcallNaked3(FunctionIDOrClientID functionIDOrClientID);
//
NESTED_ENTRY TailcallNaked3, _TEXT, NoHandler
    push ebp
    mov  ebp, esp
    pushad
    sub  esp, 16*8 /* xmm0-xmm7 */
    and  esp, (-16) /* must be alligned */
    movdqa	[esp+0x00], xmm0
    movdqa	[esp+0x10], xmm1
    movdqa	[esp+0x20], xmm2
    movdqa	[esp+0x30], xmm3
    movdqa	[esp+0x40], xmm4
    movdqa	[esp+0x50], xmm5
    movdqa	[esp+0x60], xmm6
    movdqa	[esp+0x70], xmm7
    call    EXTERNAL_C_FUNC(TailcallStub)
    movdqa	xmm0, [esp+0x00]
    movdqa	xmm1, [esp+0x10]
    movdqa	xmm2, [esp+0x20]
    movdqa	xmm3, [esp+0x30]
    movdqa	xmm4, [esp+0x40]
    movdqa	xmm5, [esp+0x50]
    movdqa	xmm6, [esp+0x60]
    movdqa	xmm7, [esp+0x70]
    lea  esp, [ebp - (14 * 8)]
    popad
    pop  ebp
    ret
NESTED_END TailcallNaked3, _TEXT
