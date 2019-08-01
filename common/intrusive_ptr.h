#ifndef INTRUSIVE_PTR_H
#define INTRUSIVE_PTR_H

#include "atomic.h"
#include "opthelpers.h"


namespace al {

template<typename T>
class intrusive_ref {
    RefCount mRef{1u};

public:
    unsigned int add_ref() noexcept { return IncrementRef(mRef); }
    unsigned int release() noexcept
    {
        auto ref = DecrementRef(mRef);
        if(UNLIKELY(ref == 0))
            delete static_cast<T*>(this);
        return ref;
    }

    /**
     * Release only if doing so would not bring the object to 0 references and
     * delete it. Returns false if the object could not be released.
     *
     * NOTE: The caller is responsible for handling a failed release, as it
     * means the object has no other references and needs to be be deleted
     * somehow.
     */
    bool releaseIfNoDelete() noexcept
    {
        auto val = mRef.load(std::memory_order_acquire);
        while(val > 1 && !mRef.compare_exchange_strong(val, val-1, std::memory_order_acq_rel))
        {
            /* val was updated with the current value on failure, so just try
             * again.
             */
        }

        return val >= 2;
    }
};

} // namespace al

#endif /* INTRUSIVE_PTR_H */
