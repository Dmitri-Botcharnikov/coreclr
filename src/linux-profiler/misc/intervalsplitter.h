#ifndef _INTERVAL_SPLITTER_H_
#define _INTERVAL_SPLITTER_H_

#include <assert.h>
#include <math.h>

class IntervalSplitter
{
public:
    IntervalSplitter() noexcept = default;

    explicit IntervalSplitter(unsigned long length) noexcept
        : m_length(length)
    {}

    IntervalSplitter(unsigned long length, unsigned long count) noexcept
        : m_length(length)
        , m_count(count)
    {}

    void Reset(unsigned long count) noexcept
    {
        m_count   = count;
        m_current = 0;
        m_index   = 0;
    }

    void Reset(unsigned long length, unsigned long count) noexcept
    {
        m_length  = length;
        m_count   = count;
        m_current = 0;
        m_index   = 0;
    }

    bool HasNext() noexcept
    {
        return m_index < m_count;
    }

    unsigned long GetNext() noexcept
    {
        assert(this->HasNext());
        unsigned long prev = m_current;
        m_current = llround(m_length * (++m_index / m_count));
        return m_current - prev;
    }

private:
    unsigned long m_length  = 0;
    double        m_count   = 0;
    unsigned long m_current = 0;
    unsigned long m_index   = 0;
};

#endif // _INTERVAL_SPLITTER_H_
