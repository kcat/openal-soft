#ifndef AL_UINTMAP_H
#define AL_UINTMAP_H

#include <unordered_map>
#include <mutex>

#include "AL/al.h"

template<typename T0, typename T1>
class ThrSafeMap {
    std::unordered_map<T0, T1> mValues;
    std::mutex mLock;

public:
    void InsertEntry(T0 key, T1 value) noexcept
    {
        std::lock_guard<std::mutex> _{mLock};
        mValues[key] = value;
    }

    T1 RemoveKey(T0 key) noexcept
    {
        T1 retval{};

        std::lock_guard<std::mutex> _{mLock};
        auto iter = mValues.find(key);
        if(iter != mValues.end())
            retval = iter->second;
        mValues.erase(iter);

        return retval;
    }
};

#endif /* AL_UINTMAP_H */
