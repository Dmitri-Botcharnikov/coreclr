.intel_syntax noprefix
#include "unixasmmacros.inc"

.extern EnterStub
.extern LeaveStub
.extern TailcallStub

.text

//
// EXTERN_C void EnterNaked3(FunctionIDOrClientID functionIDOrClientID);
//
/*NESTED_ENTRY EnterNaked3, _TEXT, NoHandler*/
.global EnterNaked3
/*.type EnterNaked3, %function*/
EnterNaked3:
    /*call EnterStub*/
    ret
/*NESTED_END EnterNaked3, _TEXT*/

//
// EXTERN_C void LeaveNaked3(FunctionIDOrClientID functionIDOrClientID);
//
/*NESTED_ENTRY LeaveNaked3, _TEXT, NoHandler*/
.global LeaveNaked3
/*.type LeaveNaked3, %function*/
LeaveNaked3:
    /*call LeaveStub*/
    ret
/*NESTED_END LeaveNaked3, _TEXT*/

//
// EXTERN_C void TailcallNaked3(FunctionIDOrClientID functionIDOrClientID);
//
/*NESTED_ENTRY TailcallNaked3, _TEXT, NoHandler*/
.global TailcallNaked3
/*.type TailcallNaked3, %function*/
TailcallNaked3:
    /*call TailcallStub*/
    ret
/*NESTED_END TailcallNaked3, _TEXT*/
