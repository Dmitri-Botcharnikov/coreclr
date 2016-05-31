#ifndef __BASEHDR_H__
#define __BASEHDR_H__

#include <pal.h>

#include <cor.h>
#include <corhdr.h>
#include <corhlpr.h>
#include <corerror.h>

#include <corpub.h>
#include <corprof.h>
#include <cordebug.h>


//
// max length for arrays
//
#define MAX_LENGTH 256


//
// export functions
//
#ifdef _USE_DLL_

    #if defined _EXPORT_
        #define DECLSPEC __declspec( dllexport )

    #elif defined _IMPORT_
        #define DECLSPEC __declspec( dllimport )
    #endif

#else
    #define DECLSPEC
#endif // _USE_DLL_


//
// DebugBreak
//
#undef _DbgBreak
#ifdef _X86_
    #define _DbgBreak() __asm { int 3 }

#else
    #define _DbgBreak() DebugBreak()
#endif // _X86_


//
// assert on false
//
#define _ASSERT_( expression ) \
{ \
    if ( !(expression) ) \
        BASEHELPER::LaunchDebugger( #expression, __FILE__, __LINE__ );  \
} \


#define DEBUG_ENVIRONMENT        "DBG_PRF"
#define LOG_ENVIRONMENT          "DBG_PRF_LOG"


//
// basic I/O macros
//
#define DISPLAY( message ) BASEHELPER::Display message;
#define DEBUG_OUT( message ) BASEHELPER::DDebug message;
#define LOG_TO_FILE( message ) BASEHELPER::LogToFile message;
#define TEXT_OUT( message ) printf( "%s", message );
#define TEXT_OUTLN( message ) printf( "%s\n", message );

#ifndef wcstombs_s
#define wcstombs_s(rval, szOut, oSize, szIn, iSize) \
    (*(rval) = WideCharToMultiByte( \
        CP_ACP, 0, szIn, oSize, szOut, iSize, 0, 0) \
    )
#endif

#endif // __BASEHDR_H__

// End of File
