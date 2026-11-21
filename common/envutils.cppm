module;

#include <optional>
#include <string>

export module envutils;

import zstring_view;

export extern "C++" namespace al {

    auto getenv(al::zstring_view envname) -> std::optional<std::string>;
#ifdef _WIN32
    auto getenv(al::wzstring_view envname) -> std::optional<std::wstring>;
#endif

} /* namespace al */
