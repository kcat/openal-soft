module;

#include "expected.hpp"

export module expected;

export namespace al {
    using al::monostate;
    using al::bad_expected_access;
    using al::unexpected;
    using al::unexpect_t;
    using al::unexpect;
    using al::expected;
}
