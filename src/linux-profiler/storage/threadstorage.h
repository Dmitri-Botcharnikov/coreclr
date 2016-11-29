#ifndef _THREAD_STORAGE_H_
#define _THREAD_STORAGE_H_

#include "livestorage.h"
#include "threadinfo.h"

class ThreadStorage : public LiveStorage<ThreadID, ThreadInfo>
{};

#endif // _THREAD_STORAGE_H_
