.intel_syntax noprefix
#include "unixasmmacros.inc"

// extern EnterStub:proc
// extern LeaveStub:proc
// extern TailcallStub:proc

//
// EXTERN_C void EnterNaked3(FunctionIDOrClientID functionIDOrClientID);
//
NESTED_ENTRY EnterNaked3, _TEXT, NoHandler
    call EnterStub
    ret

//
// EXTERN_C void LeaveNaked3(FunctionIDOrClientID functionIDOrClientID);
//
NESTED_ENTRY EnterNaked3, _TEXT, NoHandler
    call LeaveStub
    ret

//
// EXTERN_C void TailcallNaked3(FunctionIDOrClientID functionIDOrClientID);
//
NESTED_ENTRY TailcallNaked3, _TEXT, NoHandler
    call TailcallStub
    ret
