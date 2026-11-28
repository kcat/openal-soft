module;

#include "dynload.hpp"

export module dynload;

using ::GetSymbol_;

export {
#if HAVE_DYNLOAD && (defined(_WIN32) || defined(HAVE_DLFCN_H))
    using ::LibHandleStruct;
    using ::LibHandle;

    using ::LoadLib;
    using ::CloseLib;
    using ::GetSymbolAddress;
#endif
}
