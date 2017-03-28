#include <assert.h>

#include "eventchannel.h"

#define EVENT_CHANNEL_START_CAP 128 // Should be power of 2.

using Stack = EventSummary::Stack;

EventSummary::EventSummary(Stack::size_type stackSize)
    : ticks(0)
    , count(0)
    , matchPrefixSize(stackSize)
    , stackSize(stackSize)
    , ipIsChanged(false)
    , ip(0)
    , newFrames()
{}

bool EventSummary::HasStackSample() const noexcept
{
    return count > 0
        || matchPrefixSize != stackSize
        || ipIsChanged
        || newFrames.size() > 0;
}

bool EventSummary::HasAllocSample() const noexcept
{
    return allocTable.size() > 0;
}

EventChannel::EventChannel()
    : m_stack()
    , m_currentState()
    , m_buffer(EVENT_CHANNEL_START_CAP)
    , m_mutex()
    , m_bufferCapacityIncreaseIsPlanned(false)
{}

void EventChannel::IncreaseBufferCapacity()
{
    m_buffer.reserve(m_buffer.capacity() * 2);
    m_bufferCapacityIncreaseIsPlanned = false;
}

bool EventChannel::EnsureBufferCapacity(ChanCanRealloc canRealloc)
{
    bool isBufferNoSpace = m_buffer.size() == m_buffer.capacity();
    Stack::difference_type needStackSize =
        m_stack.size() - m_currentState.matchPrefixSize;
    bool isStackNoSpace = m_currentState.newFrames.capacity() < needStackSize;
    switch (canRealloc)
    {
    case ChanCanRealloc::NO:
        if (isBufferNoSpace)
        {
            this->PlanToIncreaseBufferCapacity();
        }
        assert(!isStackNoSpace);
        return !isBufferNoSpace;

    case ChanCanRealloc::YES:
        if (isBufferNoSpace || m_bufferCapacityIncreaseIsPlanned)
        {
            std::lock_guard<decltype(m_mutex)> lock(m_mutex);
            this->IncreaseBufferCapacity();
        }
        if (isStackNoSpace)
        {
            m_currentState.newFrames.reserve(needStackSize);
        }
        assert(m_buffer.capacity() != m_buffer.size());
        assert(m_currentState.newFrames.capacity() >= needStackSize);
        return true;
    }
}

void EventChannel::Push(const FunctionInfo &funcInfo) noexcept
{
    assert(m_currentState.matchPrefixSize <= m_stack.size());
    m_stack.push_back(Frame{&funcInfo, 0});

    // XXX: exception in this call will terminate process!
    this->EnsureBufferCapacity(); // Perform planned reallocation.
}

void EventChannel::Pop() noexcept
{
    assert(!m_stack.empty());
    assert(m_currentState.matchPrefixSize <= m_stack.size());
    m_stack.pop_back();
    if (m_stack.size() < m_currentState.matchPrefixSize)
    {
        m_currentState.matchPrefixSize = m_stack.size();
        m_currentState.ipIsChanged = false;
    }

    // XXX: exception in this call will terminate process!
    this->EnsureBufferCapacity(); // Perform planned reallocation.
}

void EventChannel::ChIP(UINT_PTR ip, size_t idxFromTop) noexcept
{
    assert(idxFromTop < m_stack.size());
    assert(m_currentState.matchPrefixSize <= m_stack.size());
    assert(
        m_stack.size() - idxFromTop >= m_currentState.matchPrefixSize
    );

    Frame &frame = const_cast<Frame&>(this->GetFrameFromTop(idxFromTop));
    size_t frameIdx = m_stack.size() - idxFromTop - 1;
    assert(&m_stack[frameIdx] == &frame);
    if (frame.ip != ip)
    {
        if (frameIdx + 1 == m_currentState.matchPrefixSize)
        {
            m_currentState.ipIsChanged = true;
        }
        frame.ip = ip;
    }

    // XXX: exception in this call will terminate process!
    this->EnsureBufferCapacity(); // Perform planned reallocation.
}

void EventChannel::Allocation(
    ClassInfo &classInfo, SIZE_T size, UINT_PTR ip) noexcept
{
    AllocInfo &allocInfo =
        m_currentState.allocTable[classInfo.internalId.id][ip];
    allocInfo.allocCount++;
    allocInfo.memSize += size;
}

bool EventChannel::Sample(
    DWORD ticks, ULONG count, ChanCanRealloc canRealloc) noexcept
{
    assert(m_currentState.matchPrefixSize <= m_stack.size());
    assert(m_currentState.matchPrefixSize <= m_currentState.stackSize);
    assert(!m_currentState.ipIsChanged || m_currentState.matchPrefixSize > 0);

    // XXX: exception in this call will terminate process!
    if (!this->EnsureBufferCapacity(canRealloc))
    {
        // No space for new sample.
        return false;
    }

    m_currentState.ticks = ticks;
    m_currentState.count = count;

    if (m_currentState.ipIsChanged)
    {
        m_currentState.ip = m_stack[m_currentState.matchPrefixSize - 1].ip;
    }

    assert(m_currentState.newFrames.size() == 0);
    m_currentState.newFrames.assign(
        m_stack.cbegin() + m_currentState.matchPrefixSize, m_stack.cend());

    m_buffer.push_back(std::move(m_currentState));
    m_currentState = EventSummary(m_stack.size());

    return true;
}

void EventChannel::PlanToIncreaseBufferCapacity() noexcept
{
    m_bufferCapacityIncreaseIsPlanned = true;
}

Stack::size_type EventChannel::GetStackSize() const noexcept
{
    return m_stack.size();
}

bool EventChannel::HasStackSample() const noexcept
{
    return m_currentState.HasStackSample() ||
        m_stack.size() > m_currentState.matchPrefixSize;
}

bool EventChannel::HasAllocSample() const noexcept
{
    return m_currentState.HasAllocSample();
}

const Frame &EventChannel::GetFrameFromTop(
    Stack::size_type idxFromTop) const noexcept
{
    assert(idxFromTop < m_stack.size());
    return m_stack.rbegin()[idxFromTop];
}

size_t EventChannel::GetEventSummaryCount() const noexcept
{
    return m_buffer.size();
}

const EventSummary &EventChannel::GetCurrentEventSummary() noexcept
{
    m_mutex.lock();
    return m_buffer.front();
}

void EventChannel::NextEventSummary() noexcept
{
    m_buffer.pop_front();
    m_mutex.unlock();
}
