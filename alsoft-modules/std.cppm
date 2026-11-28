module;

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <chrono>
#include <cmath>
#include <compare>
#include <condition_variable>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <ios>
#include <iostream>
#include <istream>
#include <iterator>
#include <limits>
#include <mutex>
#include <new>
#include <numbers>
#include <optional>
#include <ostream>
#include <ranges>
#include <ratio>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>

export module common.std;

/* NOLINTBEGIN(bugprone-std-namespace-modification, cert-dcl58-cpp) */
export namespace std {

    /* algorithm */
    using std::all_of;
    using std::any_of;
    using std::none_of;;
    using std::for_each;
    using std::for_each_n;
    using std::count;
    using std::count_if;
    using std::mismatch;
    using std::find;
    using std::find_if;
    using std::find_if_not;
    using std::find_end;
    using std::find_first_of;
    using std::adjacent_find;
    using std::search;
    using std::search_n;
    using std::copy;
    using std::copy_if;
    using std::copy_n;
    using std::copy_backward;
    using std::move;
    using std::move_backward;
    using std::fill;
    using std::fill_n;
    using std::transform;
    using std::generate;
    using std::generate_n;
    using std::remove;
    using std::remove_if;
    using std::remove_copy;
    using std::remove_copy_if;
    using std::replace;
    using std::replace_if;
    using std::replace_copy;
    using std::replace_copy_if;
    using std::swap_ranges;
    using std::iter_swap;
    using std::reverse;
    using std::reverse_copy;
    using std::rotate;
    using std::rotate_copy;
    using std::shift_left;
    using std::shift_right;
    using std::shuffle;
    using std::sample;
    using std::unique;
    using std::unique_copy;

    using std::is_partitioned;
    using std::partition;
    using std::partition_copy;
    using std::stable_partition;
    using std::partition_point;
    using std::is_sorted;
    using std::is_sorted_until;
    using std::sort;
    using std::partial_sort;
    using std::partial_sort_copy;
    using std::stable_sort;
    using std::nth_element;

    using std::lower_bound;
    using std::upper_bound;
    using std::binary_search;
    using std::equal_range;
    using std::merge;
    using std::inplace_merge;
    using std::includes;
    using std::set_difference;
    using std::set_intersection;
    using std::set_symmetric_difference;
    using std::set_union;

    using std::is_heap;
    using std::is_heap_until;
    using std::make_heap;
    using std::push_heap;
    using std::pop_heap;
    using std::sort_heap;

    using std::max;
    using std::max_element;
    using std::min;
    using std::min_element;
    using std::minmax;
    using std::minmax_element;
    using std::clamp;
    using std::equal;
    using std::lexicographical_compare;
    using std::lexicographical_compare_three_way;

    using std::is_permutation;
    using std::next_permutation;
    using std::prev_permutation;

    namespace ranges {
        using std::ranges::all_of;
        using std::ranges::any_of;
        using std::ranges::none_of;
        using std::ranges::for_each;
        using std::ranges::for_each_n;
        using std::ranges::count;
        using std::ranges::count_if;
        using std::ranges::mismatch;
        using std::ranges::find;
        using std::ranges::find_if;
        using std::ranges::find_if_not;
        using std::ranges::find_end;
        using std::ranges::find_first_of;
        using std::ranges::adjacent_find;
        using std::ranges::search;
        using std::ranges::search_n;

        using std::ranges::copy;
        using std::ranges::copy_if;
        using std::ranges::copy_n;
        using std::ranges::copy_backward;
        using std::ranges::move;
        using std::ranges::move_backward;
        using std::ranges::fill;
        using std::ranges::fill_n;
        using std::ranges::transform;
        using std::ranges::generate;
        using std::ranges::generate_n;
        using std::ranges::remove;
        using std::ranges::remove_if;
        using std::ranges::remove_copy;
        using std::ranges::remove_copy_if;
        using std::ranges::replace;
        using std::ranges::replace_if;
        using std::ranges::replace_copy;
        using std::ranges::replace_copy_if;
        using std::ranges::swap_ranges;
        using std::ranges::reverse;
        using std::ranges::reverse_copy;
        using std::ranges::rotate;
        using std::ranges::rotate_copy;
        using std::ranges::shuffle;
        using std::ranges::sample;
        using std::ranges::unique;
        using std::ranges::unique_copy;

        using std::ranges::is_partitioned;
        using std::ranges::partition;
        using std::ranges::partition_copy;
        using std::ranges::stable_partition;
        using std::ranges::partition_point;
        using std::ranges::is_sorted;
        using std::ranges::is_sorted_until;
        using std::ranges::sort;
        using std::ranges::partial_sort;
        using std::ranges::partial_sort_copy;
        using std::ranges::stable_sort;
        using std::ranges::nth_element;

        using std::ranges::lower_bound;
        using std::ranges::upper_bound;
        using std::ranges::binary_search;
        using std::ranges::equal_range;
        using std::ranges::merge;
        using std::ranges::inplace_merge;
        using std::ranges::includes;
        using std::ranges::set_difference;
        using std::ranges::set_intersection;
        using std::ranges::set_symmetric_difference;
        using std::ranges::set_union;

        using std::ranges::is_heap;
        using std::ranges::is_heap_until;
        using std::ranges::make_heap;
        using std::ranges::push_heap;
        using std::ranges::pop_heap;
        using std::ranges::sort_heap;

        using std::ranges::max;
        using std::ranges::max_element;
        using std::ranges::min;
        using std::ranges::min_element;
        using std::ranges::minmax;
        using std::ranges::minmax_element;
        using std::ranges::clamp;
        using std::ranges::equal;
        using std::ranges::lexicographical_compare;

        using std::ranges::is_permutation;
        using std::ranges::next_permutation;
        using std::ranges::prev_permutation;
    }

    /* array */
    using std::array;
    using std::to_array;

    /* bit */
    using std::endian;
    using std::bit_cast;
    using std::has_single_bit;
    using std::bit_ceil;
    using std::bit_floor;
    using std::bit_width;
    using std::rotl;
    using std::rotr;
    using std::countl_zero;
    using std::countl_one;
    using std::countr_zero;
    using std::countr_one;
    using std::popcount;

    /* bitset */
    using std::bitset;

    /* chrono */
    namespace chrono {
        using std::chrono::duration;
        using std::chrono::treat_as_floating_point;
        using std::chrono::duration_values;
        using std::chrono::nanoseconds;
        using std::chrono::microseconds;
        using std::chrono::milliseconds;
        using std::chrono::seconds;
        using std::chrono::minutes;
        using std::chrono::hours;
        using std::chrono::days;
        using std::chrono::weeks;
        using std::chrono::months;
        using std::chrono::years;
        using std::chrono::time_point;
#if !defined(_LIBCPP_VERSION) /* libc++ doesn't seem to have these (as of Clang 21). */
        using std::chrono::clock_time_conversion;
        using std::chrono::is_clock;
        using std::chrono::is_clock_v;
        using std::chrono::utc_clock;
        using std::chrono::tai_clock;
        using std::chrono::gps_clock;
        using std::chrono::tzdb;
        using std::chrono::tzdb_list;
        using std::chrono::time_zone;
        using std::chrono::sys_info;
        using std::chrono::local_info;
        using std::chrono::choose;
        using std::chrono::zoned_traits;
        using std::chrono::zoned_time;
        using std::chrono::time_zone_link;
        using std::chrono::nonexistent_local_time;
        using std::chrono::ambiguous_local_time;
        using std::chrono::leap_second;
        using std::chrono::leap_second_info;
        using std::chrono::from_stream;
        using std::chrono::clock_cast;
        using std::chrono::get_tzdb;
        using std::chrono::get_tzdb_list;
        using std::chrono::reload_tzdb;
        using std::chrono::remote_version;
        using std::chrono::locate_zone;
        using std::chrono::current_zone;
        using std::chrono::get_leap_second_info;
        using std::chrono::parse;
#endif
        using std::chrono::system_clock;
        using std::chrono::steady_clock;
        using std::chrono::high_resolution_clock;
        using std::chrono::file_clock;
        using std::chrono::local_t;
        using std::chrono::last_spec;
        using std::chrono::last;
        using std::chrono::day;
        using std::chrono::month;
        using std::chrono::year;
        using std::chrono::weekday;
        using std::chrono::weekday_indexed;
        using std::chrono::weekday_last;
        using std::chrono::month_day;
        using std::chrono::month_day_last;
        using std::chrono::month_weekday;
        using std::chrono::month_weekday_last;
        using std::chrono::year_month;
        using std::chrono::year_month_day;
        using std::chrono::year_month_day_last;
        using std::chrono::year_month_weekday;
        using std::chrono::year_month_weekday_last;
        using std::chrono::hh_mm_ss;

        using std::chrono::operator+;
        using std::chrono::operator-;
        using std::chrono::operator*;
        using std::chrono::operator/;
        using std::chrono::operator%;
        using std::chrono::operator==;
        using std::chrono::operator<;
        using std::chrono::operator<=;
        using std::chrono::operator>;
        using std::chrono::operator>=;
        using std::chrono::operator<=>;

        using std::chrono::duration_cast;
        using std::chrono::time_point_cast;
        using std::chrono::floor;
        using std::chrono::ceil;
        using std::chrono::round;
        using std::chrono::abs;

        using std::chrono::is_am;
        using std::chrono::is_pm;
        using std::chrono::make12;
        using std::chrono::make24;
    }
    inline namespace literals {
        inline namespace chrono_literals {
            using std::literals::chrono_literals::operator ""ns;
            using std::literals::chrono_literals::operator ""us;
            using std::literals::chrono_literals::operator ""ms;
            using std::literals::chrono_literals::operator ""s;
            using std::literals::chrono_literals::operator ""min;
            using std::literals::chrono_literals::operator ""h;
            using std::literals::chrono_literals::operator ""d;
            using std::literals::chrono_literals::operator ""y;
        }
    }

    /* cmath */
#define DECL_CMATH_OP(x) using x; using x##f; using x##l;
    using std::abs;
    DECL_CMATH_OP(std::fabs)
    DECL_CMATH_OP(std::fmod)
    DECL_CMATH_OP(std::remainder)
    DECL_CMATH_OP(std::remquo)
    DECL_CMATH_OP(std::fma)
    DECL_CMATH_OP(std::fmax)
    DECL_CMATH_OP(std::fmin)
    DECL_CMATH_OP(std::fdim)
    DECL_CMATH_OP(std::nan)
    using std::lerp;
    DECL_CMATH_OP(std::exp)
    DECL_CMATH_OP(std::exp2)
    DECL_CMATH_OP(std::expm1)
    DECL_CMATH_OP(std::log)
    DECL_CMATH_OP(std::log10)
    DECL_CMATH_OP(std::log2)
    DECL_CMATH_OP(std::log1p)
    DECL_CMATH_OP(std::pow)
    DECL_CMATH_OP(std::sqrt)
    DECL_CMATH_OP(std::cbrt)
    DECL_CMATH_OP(std::hypot)
    DECL_CMATH_OP(std::sin)
    DECL_CMATH_OP(std::cos)
    DECL_CMATH_OP(std::tan)
    DECL_CMATH_OP(std::asin)
    DECL_CMATH_OP(std::acos)
    DECL_CMATH_OP(std::atan)
    DECL_CMATH_OP(std::atan2)
    DECL_CMATH_OP(std::sinh)
    DECL_CMATH_OP(std::cosh)
    DECL_CMATH_OP(std::tanh)
    DECL_CMATH_OP(std::asinh)
    DECL_CMATH_OP(std::acosh)
    DECL_CMATH_OP(std::atanh)
    DECL_CMATH_OP(std::erf)
    DECL_CMATH_OP(std::erfc)
    DECL_CMATH_OP(std::tgamma)
    DECL_CMATH_OP(std::lgamma)
    DECL_CMATH_OP(std::ceil)
    DECL_CMATH_OP(std::floor)
    DECL_CMATH_OP(std::trunc)
    DECL_CMATH_OP(std::round)
    DECL_CMATH_OP(std::lround)
    DECL_CMATH_OP(std::llround)
    DECL_CMATH_OP(std::nearbyint)
    DECL_CMATH_OP(std::rint)
    DECL_CMATH_OP(std::lrint)
    DECL_CMATH_OP(std::llrint)
    DECL_CMATH_OP(std::frexp)
    DECL_CMATH_OP(std::ldexp)
    DECL_CMATH_OP(std::modf)
    DECL_CMATH_OP(std::scalbn)
    DECL_CMATH_OP(std::scalbln)
    DECL_CMATH_OP(std::ilogb)
    DECL_CMATH_OP(std::logb)
    DECL_CMATH_OP(std::nextafter)
    DECL_CMATH_OP(std::nexttoward)
    DECL_CMATH_OP(std::copysign)
#undef DECL_CMATH_OP
    using std::fpclassify;
    using std::isfinite;
    using std::isinf;
    using std::isnan;
    using std::isnormal;
    using std::signbit;
    using std::isgreater;
    using std::isgreaterequal;
    using std::isless;
    using std::islessequal;
    using std::islessgreater;
    using std::isunordered;

    /* compare */
    using std::three_way_comparable;
    using std::three_way_comparable_with;

    using std::partial_ordering;
    using std::weak_ordering;
    using std::strong_ordering;
    using std::common_comparison_category;
    using std::compare_three_way_result;
    using std::compare_three_way;
    using std::strong_order;
    using std::weak_order;
    using std::partial_order;
    using std::compare_strong_order_fallback;
    using std::compare_weak_order_fallback;
    using std::compare_partial_order_fallback;
    using std::is_eq;
    using std::is_neq;
    using std::is_lt;
    using std::is_lteq;
    using std::is_gt;
    using std::is_gteq;

    /* condition_variable */
    using std::condition_variable;
    using std::condition_variable_any;
    using std::cv_status;
    using std::notify_all_at_thread_exit;

    /* deque */
    using std::deque;

    /* exception */
    using std::exception;
    using std::nested_exception;
    using std::bad_exception;
    using std::terminate_handler;
    using std::exception_ptr;
    using std::uncaught_exceptions;
    using std::make_exception_ptr;
    using std::current_exception;
    using std::rethrow_exception;
    using std::throw_with_nested;
    using std::rethrow_if_nested;
    using std::terminate;
    using std::get_terminate;
    using std::set_terminate;

    /* functional */
    using std::function;
    using std::mem_fn;
    using std::reference_wrapper;
    using std::unwrap_reference;
    using std::unwrap_ref_decay;
    using std::bad_function_call;
    using std::is_bind_expression;
    using std::is_placeholder;
    using std::plus;
    using std::minus;
    using std::multiplies;
    using std::divides;
    using std::modulus;
    using std::negate;
    using std::equal_to;
    using std::not_equal_to;
    using std::greater;
    using std::less;
    using std::greater_equal;
    using std::less_equal;
    namespace ranges {
        using std::ranges::equal_to;
        using std::ranges::not_equal_to;
        using std::ranges::greater;
        using std::ranges::less;
        using std::ranges::greater_equal;
        using std::ranges::less_equal;
    }
    using std::logical_and;
    using std::logical_or;
    using std::logical_not;
    using std::bit_and;
    using std::bit_or;
    using std::bit_xor;
    using std::bit_not;
    using std::not_fn;
    using std::identity;
    using std::default_searcher;
    using std::boyer_moore_searcher;
    using std::boyer_moore_horspool_searcher;
    using std::bind_front;
    using std::bind;
    using std::ref;
    using std::cref;
    using std::invoke;

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

    /* iterator */
    using std::indirectly_readable;
    using std::indirectly_writable;
    using std::weakly_incrementable;
    using std::incrementable;
    using std::input_or_output_iterator;
    using std::sentinel_for;
    using std::sized_sentinel_for;
    using std::input_iterator;
    using std::output_iterator;
    using std::forward_iterator;
    using std::bidirectional_iterator;
    using std::random_access_iterator;
    using std::contiguous_iterator;
    using std::indirectly_unary_invocable;
    using std::indirectly_regular_unary_invocable;
    using std::indirect_unary_predicate;
    using std::indirect_binary_predicate;
    using std::indirect_equivalence_relation;
    using std::indirect_strict_weak_order;
    using std::indirectly_movable;
    using std::indirectly_movable_storable;
    using std::indirectly_copyable;
    using std::indirectly_copyable_storable;
    using std::indirectly_swappable;
    using std::indirectly_comparable;
    using std::permutable;
    using std::mergeable;
    using std::sortable;
    using std::indirect_result_t;
    using std::projected;
    using std::incrementable_traits;
    using std::indirectly_readable_traits;
    using std::iter_value_t;
    using std::iter_reference_t;
    using std::iter_difference_t;
    using std::iter_rvalue_reference_t;
    using std::iter_common_reference_t;
    using std::iterator_traits;
    using std::input_iterator_tag;
    using std::output_iterator_tag;
    using std::forward_iterator_tag;
    using std::bidirectional_iterator_tag;
    using std::random_access_iterator_tag;
    using std::contiguous_iterator_tag;
    using std::reverse_iterator;
    using std::move_iterator;
    using std::move_sentinel;
    using std::common_iterator;
    using std::default_sentinel_t;
    using std::counted_iterator;
    using std::unreachable_sentinel_t;
    using std::back_insert_iterator;
    using std::front_insert_iterator;
    using std::insert_iterator;
    using std::istream_iterator;
    using std::ostream_iterator;
    using std::istreambuf_iterator;
    using std::ostreambuf_iterator;
    using std::unreachable_sentinel;
    using std::default_sentinel;
    using std::make_reverse_iterator;
    using std::make_move_iterator;
    using std::front_inserter;
    using std::back_inserter;
    using std::inserter;
    using std::advance;
    using std::distance;
    using std::next;
    using std::prev;
    namespace ranges {
#if defined(__GLIBCXX__)
        /* HACK: Implementation detail for libstdc++, which uses objects with a
         * function call operator in an inline namespace to avoid collisions
         * with friend functions of the same names.
         */
        inline namespace _Cpo {
            using _Cpo::iter_move;
            using _Cpo::iter_swap;
        }
#elif defined(_MSVC_STL_UPDATE)
        /* HACK: MS STL does something similar. */
        inline namespace _Cpos {
            using _Cpos::iter_move;
            using _Cpos::iter_swap;
        }
#else
        using std::ranges::iter_move;
        using std::ranges::iter_swap;
#endif
        using std::ranges::advance;
        using std::ranges::distance;
        using std::ranges::next;
        using std::ranges::prev;
    }

    /* limits */
    using std::numeric_limits;
    using std::float_round_style;
    using std::float_denorm_style;

    /* mutex */
    using std::mutex;
    using std::timed_mutex;
    using std::recursive_mutex;
    using std::recursive_timed_mutex;
    using std::lock_guard;
    using std::unique_lock;
    using std::scoped_lock;
    using std::once_flag;
    using std::try_lock;
    using std::lock;
    using std::call_once;
    using std::defer_lock;
    using std::try_to_lock;
    using std::adopt_lock;
    using std::defer_lock_t;
    using std::try_to_lock_t;
    using std::adopt_lock_t;

    /* new */
    using std::bad_alloc;
    using std::bad_array_new_length;
    using std::align_val_t;
    using std::new_handler;
    using std::nothrow;
    using std::nothrow_t;
    using std::destroying_delete;
    using std::destroying_delete_t;
    using std::hardware_destructive_interference_size;
    using std::hardware_constructive_interference_size;
    using std::launder;

    /* numbers */
    namespace numbers {
        using std::numbers::e_v;
        using std::numbers::log2e_v;
        using std::numbers::log10e_v;
        using std::numbers::pi_v;
        using std::numbers::inv_pi_v;
        using std::numbers::inv_sqrtpi_v;
        using std::numbers::ln2_v;
        using std::numbers::ln10_v;
        using std::numbers::sqrt2_v;
        using std::numbers::sqrt3_v;
        using std::numbers::inv_sqrt3_v;
        using std::numbers::egamma_v;
        using std::numbers::phi_v;
        using std::numbers::e;
        using std::numbers::log2e;
        using std::numbers::log10e;
        using std::numbers::pi;
        using std::numbers::inv_pi;
        using std::numbers::inv_sqrtpi;
        using std::numbers::ln2;
        using std::numbers::ln10;
        using std::numbers::sqrt2;
        using std::numbers::sqrt3;
        using std::numbers::inv_sqrt3;
        using std::numbers::egamma;
        using std::numbers::phi;
    }

    /* optional */
    using std::optional;
    using std::bad_optional_access;
    using std::nullopt_t;
    using std::nullopt;
    using std::make_optional;

    /* ranges */
    namespace ranges {
        using std::ranges::range;
        using std::ranges::borrowed_range;
        using std::ranges::sized_range;
        using std::ranges::view;
        using std::ranges::input_range;
        using std::ranges::output_range;
        using std::ranges::forward_range;
        using std::ranges::bidirectional_range;
        using std::ranges::random_access_range;
        using std::ranges::contiguous_range;
        using std::ranges::common_range;
        using std::ranges::viewable_range;
        using std::ranges::iterator_t;
        using std::ranges::sentinel_t;
        using std::ranges::range_difference_t;
        using std::ranges::range_size_t;
        using std::ranges::range_value_t;
        using std::ranges::range_reference_t;
        using std::ranges::range_rvalue_reference_t;
        using std::ranges::range_common_reference_t;
        using std::ranges::view_interface;
        using std::ranges::subrange;
        using std::ranges::dangling;
        using std::ranges::borrowed_iterator_t;
        using std::ranges::borrowed_subrange_t;
        using std::ranges::empty_view;
        using std::ranges::single_view;
        using std::ranges::iota_view;
        using std::ranges::basic_istream_view;
        using std::ranges::istream_view;
        using std::ranges::wistream_view;
        using std::ranges::ref_view;
        using std::ranges::owning_view;
        using std::ranges::filter_view;
        using std::ranges::transform_view;
        using std::ranges::take_view;
        using std::ranges::take_while_view;
        using std::ranges::drop_view;
        using std::ranges::drop_while_view;
        using std::ranges::join_view;
        using std::ranges::lazy_split_view;
        using std::ranges::split_view;
        using std::views::counted;
        using std::ranges::common_view;
        using std::ranges::reverse_view;
        using std::ranges::elements_view;
        using std::ranges::keys_view;
        using std::ranges::values_view;
        using std::ranges::begin;
        using std::ranges::end;
        using std::ranges::cbegin;
        using std::ranges::cend;
        using std::ranges::rbegin;
        using std::ranges::rend;
        using std::ranges::crbegin;
        using std::ranges::crend;
        using std::ranges::size;
        using std::ranges::ssize;
        using std::ranges::empty;
        using std::ranges::data;
        using std::ranges::cdata;
        using std::ranges::subrange_kind;
        using std::ranges::get;
        namespace views {
            using std::ranges::views::empty;
            using std::ranges::views::single;
            using std::ranges::views::iota;
            using std::ranges::views::istream;
            using std::ranges::views::all_t;
            using std::ranges::views::all;
            using std::ranges::views::filter;
            using std::ranges::views::transform;
            using std::ranges::views::take;
            using std::ranges::views::take_while;
            using std::ranges::views::drop;
            using std::ranges::views::drop_while;
            using std::ranges::views::join;
            using std::ranges::views::lazy_split;
            using std::ranges::views::split;
            using std::ranges::views::counted;
            using std::ranges::views::common;
            using std::ranges::views::reverse;
            using std::ranges::views::elements;
            using std::ranges::views::keys;
            using std::ranges::views::values;
        }
#if defined(_LIBCPP_VERSION)
        /* Implementation detail for libc++, which uses this accessible
         * operator to handle piping adaptors.
         */
        using std::ranges::operator|;
#elif defined(_MSVC_STL_UPDATE)
        namespace _Pipe {
            using _Pipe::operator|;
        }
#endif
    }
    namespace views = std::ranges::views;
    using std::ranges::get;

    /* ratio */
    using std::ratio;
    using std::ratio_add;
    using std::ratio_subtract;
    using std::ratio_multiply;
    using std::ratio_divide;
    using std::ratio_equal;
    using std::ratio_not_equal;
    using std::ratio_less;
    using std::ratio_less_equal;
    using std::ratio_greater;
    using std::ratio_greater_equal;
    using std::atto;
    using std::femto;
    using std::pico;
    using std::nano;
    using std::micro;
    using std::milli;
    using std::centi;
    using std::deci;
    using std::deca;
    using std::hecto;
    using std::kilo;
    using std::mega;
    using std::giga;
    using std::tera;
    using std::peta;
    using std::exa;

    /* source_location */
    using std::source_location;

    /* stdexcept */
    using std::logic_error;
    using std::invalid_argument;
    using std::domain_error;
    using std::length_error;
    using std::out_of_range;
    using std::runtime_error;
    using std::range_error;
    using std::overflow_error;
    using std::underflow_error;

    /* string */
    using std::char_traits;
    using std::basic_string;
    using std::string;
    using std::u8string;
    using std::u16string;
    using std::u32string;
    using std::wstring;
    using std::getline;
    using std::stoi;
    using std::stol;
    using std::stoll;
    using std::stoul;
    using std::stoull;
    using std::stof;
    using std::stod;
    using std::stold;
    using std::to_string;
    using std::to_wstring;
    inline namespace literals {
        inline namespace string_literals {
            using std::literals::string_literals::operator ""s;
        }
    }

    /* thread */
    using std::thread;
    using std::jthread;

    namespace this_thread {
        using std::this_thread::yield;
        using std::this_thread::get_id;
        using std::this_thread::sleep_for;
        using std::this_thread::sleep_until;
    }

    /* tuple */
    using std::tuple;
    using std::tuple_size;
    using std::tuple_element;
    using std::tuple_size;
    using std::tuple_element;
    using std::uses_allocator;
    using std::ignore;
    using std::make_tuple;
    using std::tie;
    using std::forward_as_tuple;
    using std::tuple_cat;
    using std::apply;
    using std::make_from_tuple;

    /* utility */
    using std::cmp_equal;
    using std::cmp_not_equal;
    using std::cmp_greater;
    using std::cmp_less;
    using std::cmp_greater_equal;
    using std::cmp_less_equal;
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

    /* variant */
    using std::variant;
    using std::monostate;
    using std::bad_variant_access;
    using std::variant_size;
    using std::variant_size_v;
    using std::variant_alternative;
    using std::variant_alternative_t;
    using std::variant_npos;
    using std::visit;
    using std::holds_alternative;
    using std::get_if;

    /* General purpose and specialized methods and types. */
    using std::begin;
    using std::cbegin;
    using std::end;
    using std::cend;
    using std::rbegin;
    using std::crbegin;
    using std::rend;
    using std::crend;
    using std::size;
    using std::ssize;
    using std::empty;
    using std::data;

    using std::erase;
    using std::erase_if;
    using std::get;
    using std::hash;
    using std::common_type;
    using std::common_type_t;
    using std::formatter;
    using std::swap;

    using std::operator+;
    using std::operator&;
    using std::operator|;
    using std::operator^;
    using std::operator>>;
    using std::operator<<;
    using std::operator==;
    using std::operator!=;
    using std::operator<;
    using std::operator<=;
    using std::operator>;
    using std::operator>=;
    using std::operator<=>;

}
/* NOLINTEND(bugprone-std-namespace-modification, cert-dcl58-cpp) */

export {
    /* new */
    using ::operator new;
    using ::operator new[];
    using ::operator delete;
    using ::operator delete[];
}
