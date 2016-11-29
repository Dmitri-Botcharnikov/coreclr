#ifndef _LIVE_STORAGE_H_
#define _LIVE_STORAGE_H_

#include <set>

#include "mappedstorage.h"
#include "iterator_range.h"

template<typename ID, typename INFO>
class LiveStorage : public MappedStorage<ID, INFO>
{
private:
    using Base = MappedStorage<ID, INFO>;

    struct less_info
    {
        bool operator()(const INFO &lhs, const INFO &rhs) const
        {
            return lhs.internalId.id < rhs.internalId.id;
        }
    };

public:
    typedef std::set<std::reference_wrapper<INFO>, less_info> LiveContainer;
    typedef typename LiveContainer::iterator live_iterator;
    typedef typename LiveContainer::const_iterator const_live_iterator;
    typedef iterator_range<live_iterator> live_iterator_range;
    typedef iterator_range<const_live_iterator> const_live_iterator_range;

    std::pair<INFO&, bool>
    Place(ID id)
    {
        auto res = this->Base::Place(id);
        if (res.second)
        {
            m_liveStorage.insert(std::ref(res.first));
        }
        return std::make_pair(std::ref(res.first), res.second);
    }

    INFO &Unlink(ID id)
    {
        auto &res = this->Base::Unlink(id);
        m_liveStorage.erase(std::ref(res));
        return res;
    }

    live_iterator_range GetLiveRange()
    {
        return live_iterator_range(m_liveStorage.begin(), m_liveStorage.end());
    }

    const_live_iterator_range GetLiveRange() const
    {
        return const_live_iterator_range(
            m_liveStorage.begin(), m_liveStorage.end());
    }

    LiveContainer GetLiveContainer() const
    {
        return m_liveStorage;
    }

protected:
    LiveContainer m_liveStorage;
};

#endif // _LIVE_STORAGE_H_
