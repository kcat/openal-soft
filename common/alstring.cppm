module;

#include "alstring.h"

export module alstring;

export {
#if defined(_WIN32)
    using ::utf8_to_wstr;
    using ::wstr_to_utf8;
#endif

    namespace al {
        using al::ci_compare;
        using al::char_as_u8;
        using al::contains;
        using al::u8_as_char;
    }
}
