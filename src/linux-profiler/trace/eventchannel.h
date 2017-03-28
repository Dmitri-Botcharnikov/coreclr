#ifndef _EVENT_CHANNEL_H_
#define _EVENT_CHANNEL_H_

#include <utility>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>

#include "functioninfo.h"
#include "classinfo.h"
#include "ringbuffer.h"

struct Frame
{
    const FunctionInfo *pFuncInfo;
    UINT_PTR ip;
};

struct AllocInfo
{
    SIZE_T allocCount = 0;
    SIZE_T memSize    = 0;
};

typedef std::map<ULONG, std::map<UINT_PTR, AllocInfo>> AllocTable;

struct EventSummary
{
    typedef std::vector<Frame> Stack;

    explicit EventSummary(Stack::size_type stackSize = 0);

    //
    // Sample
    //

    DWORD                  ticks;
    ULONG                  count;

    //
    // Stack
    //

    Stack::difference_type matchPrefixSize;
    Stack::size_type       stackSize;
    bool                   ipIsChanged;
    UINT_PTR               ip;
    Stack                  newFrames;

    bool HasStackSample() const noexcept;

    //
    // Allocations
    //

    AllocTable             allocTable;

    bool HasAllocSample() const noexcept;
};

enum class ChanCanRealloc
{
    NO,
    YES
};

class EventChannel
{
public:
    typedef EventSummary::Stack Stack;

    EventChannel();

private:
    void IncreaseBufferCapacity();

    bool EnsureBufferCapacity(ChanCanRealloc canRealloc = ChanCanRealloc::YES);

public:
    //
    // Writer methods.
    //

    void Push(const FunctionInfo &funcInfo) noexcept;

    void Pop() noexcept;

    void ChIP(UINT_PTR ip, size_t idxFromTop = 0) noexcept;

    void Allocation(
        ClassInfo &classInfo, SIZE_T size, UINT_PTR ip = 0) noexcept;

    bool Sample(
        DWORD ticks, ULONG count,
        ChanCanRealloc canRealloc = ChanCanRealloc::YES) noexcept;

    void PlanToIncreaseBufferCapacity() noexcept;

    Stack::size_type GetStackSize() const noexcept;

    const Frame &GetFrameFromTop(
        Stack::size_type idxFromTop = 0) const noexcept;

    bool HasStackSample() const noexcept;

    bool HasAllocSample() const noexcept;

    //
    // Reader methods.
    //

    size_t GetEventSummaryCount() const noexcept;

    // Reference only valid until next call to NextEventSummary().
    const EventSummary &GetCurrentEventSummary() noexcept;

    void NextEventSummary() noexcept;

private:
    Stack m_stack;
    EventSummary m_currentState;

    ring_buffer<EventSummary> m_buffer;
    std::mutex m_mutex;
    bool m_bufferCapacityIncreaseIsPlanned;
};

#endif // _EVENT_CHANNEL_H_
