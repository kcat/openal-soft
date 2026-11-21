module;

#include <algorithm>
#include <compare>
#include <exception>
#include <fstream>
#include <ios>
#include <iostream>
#include <istream>
#include <mutex>
#include <new>
#include <optional>
#include <ostream>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <thread>
#include <tuple>
#include <utility>

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

    /* optional */
    using std::optional;
    using std::bad_optional_access;
    using std::nullopt_t;
    using std::nullopt;
    using std::make_optional;

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

    using std::get;
    using std::hash;
    using std::swap;
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
