module;

#include "fmt/format.h"
#include "fmt/ranges.h"
#include "fmt/std.h"

export module fmtlib;

export FMT_BEGIN_NAMESPACE

    using fmt::format;
    using fmt::formatter;
    using fmt::format_args;
    using fmt::format_string;
    using fmt::join;
    using fmt::make_format_args;
    using fmt::print;
    using fmt::println;
    using fmt::string_view;
    using fmt::vformat;
    using fmt::vprint;

FMT_END_NAMESPACE
