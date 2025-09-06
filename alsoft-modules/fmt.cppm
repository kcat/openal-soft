module;

#include "fmt/base.h"
#include "fmt/chrono.h"
#include "fmt/format.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h"
#include "fmt/std.h"

export module alsoft.fmt;

export namespace fmt {

using fmt::format;
using fmt::format_args;
using fmt::format_string;
using fmt::join;
using fmt::make_format_args;
using fmt::print;
using fmt::println;
using fmt::string_view;
using fmt::vformat;

} /* namespace fmt */
