// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
//*****************************************************************************
// File: gdbjit.h
// 

//
// Header file for GDB JIT interface implemenation.
//
//*****************************************************************************


#ifndef __GDBJIT_H__
#define __GDBJIT_H__

#include "method.hpp"

class NotifyGdb
{
public:
    static void MethodCompiled(MethodDesc* MethodDescPtr);
    static void MethodDropped(MethodDesc* MethodDescPtr);
private:
    struct MemBuf
    {
        void* MemPtr;
        unsigned MemSize;
    };
    
    static bool BuildELFHeader();
};

#endif // #ifndef __GDBJIT_H__
