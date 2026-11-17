module;

#include <fstream>
#include <ios>
#include <iostream>
#include <istream>
#include <ostream>
#include <source_location>
#include <sstream>
#include <streambuf>
#include <utility>

export module common.std;

/* NOLINTBEGIN(bugprone-std-namespace-modification, cert-dcl58-cpp) */
export namespace std {

    /* ios, iostream, etc... */
    using std::cerr;
    using std::cin;
    using std::clog;
    using std::cout;

    using std::hex;
    using std::dec;
    using std::oct;
    using std::fixed;
    using std::flush;
    using std::endl;

    using std::ios_base;
    using std::basic_ios;
    using std::basic_streambuf;
    using std::basic_ostream;
    using std::basic_istream;
    using std::basic_iostream;

    using std::basic_filebuf;
    using std::basic_ofstream;
    using std::basic_ifstream;
    using std::basic_fstream;

    using std::basic_stringbuf;
    using std::basic_ostringstream;
    using std::basic_istringstream;
    using std::basic_stringstream;

    using std::streamoff;
    using std::streamsize;
    using std::fpos;
    using std::streampos;

    using std::ios;
    using std::streambuf;
    using std::istream;
    using std::ostream;
    using std::iostream;

    using std::filebuf;
    using std::ofstream;
    using std::ifstream;
    using std::fstream;

    using std::stringbuf;
    using std::ostringstream;
    using std::istringstream;
    using std::stringstream;

    using std::io_errc;
    using std::iostream_category;

    /* utility */
    using std::cmp_equal;
    using std::cmp_not_equal;
    using std::cmp_greater;
    using std::cmp_less;
    using std::cmp_greater_equal;
    using std::cmp_less_equal;

    using std::swap;
    using std::forward;
    using std::exchange;
    using std::move;
    using std::move_if_noexcept;
    using std::as_const;
    using std::declval;
    using std::in_range;
    using std::make_pair;
    using std::get;
    using std::pair;
    using std::tuple;
    using std::tuple_size;
    using std::tuple_element;
    using std::tuple_size;
    using std::tuple_element;
    using std::integer_sequence;
    using std::ignore;
    using std::piecewise_construct;
    using std::piecewise_construct_t;
    using std::in_place;
    using std::in_place_type;
    using std::in_place_index;
    using std::in_place_t;
    using std::in_place_type_t;
    using std::in_place_index_t;

    using std::source_location;

    using std::operator>>;
    using std::operator<<;
    using std::operator==;
    using std::operator<=>;

}
/* NOLINTEND(bugprone-std-namespace-modification, cert-dcl58-cpp) */
