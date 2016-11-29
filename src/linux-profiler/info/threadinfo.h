#ifndef _THREAD_INFO_H_
#define _THREAD_INFO_H_

#include <atomic>

#include <signal.h>
#include <pthread.h>

#include <cor.h>
#include <corhdr.h>
#include <corprof.h>

#include "eventchannel.h"
#include "mappedinfo.h"

struct ThreadInfo : public MappedInfo<ThreadID>
{
    DWORD                 osThreadId;
    pthread_t             nativeHandle;

    DWORD64               lastUserTime;

    EventChannel          eventChannel;
    std::atomic_ulong     genTicks;
    ULONG                 fixTicks;
    volatile sig_atomic_t interruptible;
    size_t                maxRestoreIpIdx;

    ThreadInfo()
        : osThreadId(0)
        , nativeHandle()
        , lastUserTime(0)
        , eventChannel()
        , genTicks(0)
        , fixTicks(0)
        , interruptible(false)
        , maxRestoreIpIdx(0)
    {}
};

#endif // _THREAD_INFO_H_
