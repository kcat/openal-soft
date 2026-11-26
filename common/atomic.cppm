module;

#include "atomic.hpp"

export module atomic;

export {
    using ::IncrementRef;
    using ::DecrementRef;

    using ::AtomicReplaceHead;

    namespace al {
        using al::atomic_wait;
        using al::atomic_notify_one;
        using al::atomic_notify_all;
        using al::atomic_unique_ptr;
    }
}
