.intel_syntax noprefix
#include "unixasmmacros.inc"

//
// EXTERN_C void EnterNaked3(FunctionIDOrClientID functionIDOrClientID);
//
NESTED_ENTRY EnterNaked3, _TEXT, NoHandler
    push    rbp
    .cfi_def_cfa_offset 16
    .cfi_offset 6, -16
    mov     rbp, rsp
    .cfi_def_cfa_register 6
    sub     rsp, 16
    mov     QWORD PTR [rbp-8], rdi
    mov     eax, 0
    call    EXTERNAL_C_FUNC(EnterStub)
    nop
    leave
    .cfi_def_cfa 7, 8
    ret
NESTED_END EnterNaked3, _TEXT

//
// EXTERN_C void LeaveNaked3(FunctionIDOrClientID functionIDOrClientID);
//
NESTED_ENTRY LeaveNaked3, _TEXT, NoHandler
    push    rbp
    .cfi_def_cfa_offset 16
    .cfi_offset 6, -16
    mov     rbp, rsp
    .cfi_def_cfa_register 6
    sub     rsp, 16
    mov     QWORD PTR [rbp-8], rdi
    mov     eax, 0
    call    EXTERNAL_C_FUNC(LeaveStub)
    nop
    leave
    .cfi_def_cfa 7, 8
    ret
NESTED_END LeaveNaked3, _TEXT

//
// EXTERN_C void TailcallNaked3(FunctionIDOrClientID functionIDOrClientID);
//
NESTED_ENTRY TailcallNaked3, _TEXT, NoHandler
    push    rbp
    .cfi_def_cfa_offset 16
    .cfi_offset 6, -16
    mov     rbp, rsp
    .cfi_def_cfa_register 6
    sub     rsp, 16
    mov     QWORD PTR [rbp-8], rdi
    mov     eax, 0
    call    EXTERNAL_C_FUNC(TailcallStub)
    nop
    leave
    .cfi_def_cfa 7, 8
    ret
NESTED_END TailcallNaked3, _TEXT
