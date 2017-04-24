.intel_syntax noprefix
#include "unixasmmacros.inc"

//
// EXTERN_C void EnterNaked3(FunctionIDOrClientID functionIDOrClientID);
//
NESTED_ENTRY EnterNaked3, _TEXT, NoHandler
    PROLOG_BEG
    PROLOG_PUSH eax
    PROLOG_PUSH ebx
    PROLOG_PUSH ecx
    PROLOG_PUSH edx
    PROLOG_END
    
    mov ebx, [ebp+0x8]
    push ebx
    call    C_FUNC(EnterStub)
    
    EPILOG_BEG
    EPILOG_POP edx
    EPILOG_POP ecx
    EPILOG_POP ebx
    EPILOG_POP eax
    EPILOG_END
    ret 4
NESTED_END EnterNaked3, _TEXT

//
// EXTERN_C void LeaveNaked3(FunctionIDOrClientID functionIDOrClientID);
//
NESTED_ENTRY LeaveNaked3, _TEXT, NoHandler
    PROLOG_BEG
    PROLOG_PUSH eax
    PROLOG_PUSH ebx
    PROLOG_PUSH ecx
    PROLOG_PUSH edx
    PROLOG_END
    
    mov ebx, [ebp+0x8]
    push ebx
    call    C_FUNC(LeaveStub)
    
    EPILOG_BEG
    EPILOG_POP edx
    EPILOG_POP ecx
    EPILOG_POP ebx
    EPILOG_POP eax
    EPILOG_END
    ret 4
NESTED_END LeaveNaked3, _TEXT

//
// EXTERN_C void TailcallNaked3(FunctionIDOrClientID functionIDOrClientID);
//
NESTED_ENTRY TailcallNaked3, _TEXT, NoHandler
    PROLOG_BEG
    PROLOG_PUSH eax
    PROLOG_PUSH ebx
    PROLOG_PUSH ecx
    PROLOG_PUSH edx
    PROLOG_END
    
    mov ebx, [ebp+0x8]
    push ebx
    call    C_FUNC(TailcallStub)
    
    EPILOG_BEG
    EPILOG_POP edx
    EPILOG_POP ecx
    EPILOG_POP ebx
    EPILOG_POP eax
    EPILOG_END
    ret 4
NESTED_END TailcallNaked3, _TEXT
