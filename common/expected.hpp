#ifndef AL_EXPECTED_HPP
#define AL_EXPECTED_HPP

#include <exception>
#include <type_traits>
#include <utility>

#include "opthelpers.h"

namespace al {

struct monostate { };

template<typename Er>
class bad_expected_access;

template<> /* NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor) */
class bad_expected_access<void> : public std::exception {
protected:
    bad_expected_access() noexcept = default;
    bad_expected_access(bad_expected_access const&) noexcept = default;
    bad_expected_access(bad_expected_access&&) noexcept = default;
    auto operator=(bad_expected_access const&) noexcept LIFETIMEBOUND -> bad_expected_access& = default;
    auto operator=(bad_expected_access&&) noexcept LIFETIMEBOUND -> bad_expected_access& = default;
    ~bad_expected_access() override = default;

public:
    [[nodiscard]]
    auto what() const noexcept -> const char* override
    { return "accessing al::expected with no value"; }
};

template<typename Er>
class bad_expected_access : public bad_expected_access<void> {
    Er mError;

public:
    explicit bad_expected_access(Er error) : mError{std::move(error)} { }

    [[nodiscard]] auto error() const& noexcept -> Er const& { return mError; }
    [[nodiscard]] auto error() & noexcept -> Er& { return mError; }
    [[nodiscard]] auto error() const&& noexcept -> Er const&& { return std::move(mError); }
    [[nodiscard]] auto error() && noexcept -> Er&& { return std::move(mError); }
};


template<typename E>
class unexpected {
    E mError;

public:
    constexpr unexpected(const unexpected&) = default;
    constexpr unexpected(unexpected&&) = default;
    template<typename E2=E> requires(!std::is_same_v<std::remove_cvref_t<E2>, unexpected>
        && !std::is_same_v<std::remove_cvref_t<E2>, std::in_place_t>
        && std::is_constructible_v<E, E2>)
    constexpr explicit unexpected(E2&& rhs) : mError{std::forward<E2>(rhs)}
    { }
    template<typename ...Args> requires(std::is_constructible_v<E, Args...>)
    constexpr explicit unexpected(std::in_place_t, Args&& ...args)
        : mError{std::forward<Args>(args)...}
    { }
    template<typename U, typename ...Args>
        requires(std::is_constructible_v<E, std::initializer_list<U>&, Args...>)
    constexpr explicit unexpected(std::in_place_t, std::initializer_list<U> il, Args&& ...args)
        : mError{il, std::forward<Args>(args)...}
    { }

    [[nodiscard]] constexpr auto error() const& noexcept -> const E& { return mError; }
    [[nodiscard]] constexpr auto error() & noexcept -> E& { return mError; }
    [[nodiscard]] constexpr auto error() const&& noexcept -> const E&& { return std::move(mError); }
    [[nodiscard]] constexpr auto error() && noexcept -> E&& { return std::move(mError); }

    constexpr void swap(unexpected& other) noexcept(std::is_nothrow_swappable_v<E>)
    { std::swap(mError, other.mError); }

    template<typename E2>
    friend constexpr auto operator==(const unexpected& lhs, const unexpected<E2>& rhs) -> bool
    { return lhs.error() == rhs.error(); }

    friend constexpr void swap(unexpected& lhs, unexpected& rhs) noexcept(noexcept(lhs.swap(rhs)))
    { lhs.swap(rhs); }
};

template<typename E>
unexpected(E) -> unexpected<E>;


struct unexpect_t { };
inline constexpr auto unexpect = unexpect_t{};


namespace detail_ {
    /* Internal tag type to construct an expected with an invocable. */
    struct in_place_inv_ { };
    struct unexpect_inv_ { };
}

template<typename Ty, typename Er>
class [[nodiscard]] expected {
    static constexpr auto void_success = std::is_void_v<Ty>;

    using S = std::conditional_t<void_success, monostate, Ty>;
    union {
        S mValue;
        Er mError;
    };
    bool mHasValue;

    /* NOLINTBEGIN(cppcoreguidelines-pro-type-union-access) */
    auto check_object() const -> void { if(not mHasValue) throw bad_expected_access<Er>{mError}; }

#if defined(_GLIBCXX_DEBUG_ASSERT)
#define assert_object(x, msg) _GLIBCXX_DEBUG_ASSERT(x)
#elif defined(_LIBCPP_ASSERT_VALID_ELEMENT_ACCESS)
#define assert_object(x, msg) _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(x, msg)
#elif defined( _STL_VERIFY) && (_MSVC_STL_HARDENING_EXPECTED || _ITERATOR_DEBUG_LEVEL != 0)
#define assert_object(x, msg) _STL_VERIFY(x, msg)
#else
#define assert_object(x, msg) static_cast<void>(0)
#endif

    /* Internal constructor to initialize from a callable without a copy/move
     * of the returned object (or assigns nothing for a void success type).
     */
    template<typename, typename> friend class expected;

    using in_place_inv_ = detail_::in_place_inv_;
    using unexpect_inv_ = detail_::unexpect_inv_;

    template<typename F> requires(not void_success) explicit constexpr
    expected(in_place_inv_, F&& f) : mValue{std::invoke(std::forward<F>(f))}, mHasValue{true}
    { }
    template<typename F> requires(void_success) explicit constexpr
    expected(in_place_inv_, F&& f) : mValue{}, mHasValue{true}
    { std::invoke(std::forward<F>(f)); }
    template<typename F> explicit constexpr
    expected(unexpect_inv_, F&& f) : mError{std::invoke(std::forward<F>(f))}, mHasValue{false}
    { }

public:
    constexpr
    expected() noexcept(std::is_nothrow_default_constructible_v<S>) : mValue{}, mHasValue{true}
    { }
    constexpr expected(expected const &rhs)
        noexcept(std::is_nothrow_copy_constructible_v<S>
            and std::is_nothrow_copy_constructible_v<Er>)
        requires(std::is_trivially_copy_constructible_v<S>
            and std::is_trivially_copy_constructible_v<Er>)
        = default;
    constexpr expected(expected&& rhs)
        noexcept(std::is_nothrow_move_constructible_v<S>
            and std::is_nothrow_move_constructible_v<Er>)
        requires(std::is_trivially_move_constructible_v<S>
            and std::is_trivially_move_constructible_v<Er>)
        = default;
    constexpr expected(expected const &rhs)
        noexcept(std::is_nothrow_copy_constructible_v<S>
            and std::is_nothrow_copy_constructible_v<Er>)
        requires(not std::is_trivially_copy_constructible_v<S>
            or not std::is_trivially_copy_constructible_v<Er>)
        : mHasValue{rhs.mHasValue}
    {
        if(rhs.mHasValue) std::construct_at(&mValue, rhs.mValue);
        else std::construct_at(&mError, rhs.mError);
    }
    constexpr expected(expected&& rhs)
        noexcept(std::is_nothrow_move_constructible_v<S>
            and std::is_nothrow_move_constructible_v<Er>)
        requires(not std::is_trivially_move_constructible_v<S>
            or not std::is_trivially_move_constructible_v<Er>)
        : mHasValue{rhs.mHasValue}
    {
        if(rhs.mHasValue) std::construct_at(&mValue, std::move(rhs.mValue));
        else std::construct_at(&mError, std::move(rhs.mError));
    }
    constexpr ~expected()
    {
        if(mHasValue) std::destroy_at(&mValue);
        else std::destroy_at(&mError);
    }
    constexpr ~expected() requires(std::is_trivially_destructible_v<S> and std::is_trivially_destructible_v<Er>) = default;

    /* Value constructors */
    template<typename U=std::remove_cv_t<Ty>>
        requires(not std::is_same_v<std::remove_cvref_t<U>, std::in_place_t>
            and not std::is_same_v<expected, std::remove_cvref_t<U>>
            and std::is_constructible_v<Ty, U>)
        constexpr explicit(!std::is_convertible_v<U, Ty>)
    expected(U&& v) : mValue{std::forward<U>(v)}, mHasValue{true} { }

    template<typename ...Args> requires(not void_success and std::is_constructible_v<Ty, Args...>)
        constexpr explicit
    expected(std::in_place_t, Args&& ...args)
        : mValue{std::forward<Args>(args)...}, mHasValue{true}
    { }

    constexpr explicit
    expected(std::in_place_t) noexcept requires(void_success) : mValue{}, mHasValue{true} { }

    /* Error constructors */
    template<typename U> requires(std::is_constructible_v<Er, const U&>) constexpr
        explicit(not std::is_convertible_v<const U&, Er>)
    expected(unexpected<U> const &rhs) : mError{rhs.error()}, mHasValue{false} { }

    template<typename U> requires(std::is_constructible_v<Er, U>) constexpr
        explicit(not std::is_convertible_v<U, Er>)
    expected(unexpected<U>&& rhs) : mError{std::move(rhs).error()}, mHasValue{false} { }

    template<typename ...Args> requires(std::is_constructible_v<Er, Args...>) constexpr explicit
    expected(unexpect_t, Args&& ...args) : mError{std::forward<Args>(args)...}, mHasValue{false}
    { }

    template<typename ...Args> requires(std::is_nothrow_constructible_v<Ty, Args...>) constexpr
    auto emplace(Args&& ...args) & noexcept LIFETIMEBOUND -> expected&
    {
        if(mHasValue) std::destroy_at(&mValue);
        else std::destroy_at(&mError);
        std::construct_at(&mValue, std::forward<Args>(args)...);
        mHasValue = true;
        return *this;
    }

    [[nodiscard]] constexpr auto has_value() const noexcept -> bool { return mHasValue; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] constexpr
    auto operator*() & noexcept -> S& requires(not void_success)
    { assert_object(has_value(), "expected::operator* called without a value"); return mValue; }
    [[nodiscard]] constexpr
    auto operator*() const& noexcept -> S const& requires(not void_success)
    { assert_object(has_value(), "expected::operator* called without a value"); return mValue; }
    [[nodiscard]] constexpr
    auto operator*() && noexcept -> S&& requires(not void_success)
    {
        assert_object(has_value(), "expected::operator* called without a value");
        return std::move(mValue);
    }
    [[nodiscard]] constexpr
    auto operator*() const&& noexcept -> S const&& requires(not void_success)
    {
        assert_object(has_value(), "expected::operator* called without a value");
        return std::move(mValue);
    }

    [[nodiscard]] constexpr
    auto operator->() noexcept -> S* requires(not void_success)
    { assert_object(has_value(), "expected::operator-> called without a value"); return &mValue; }
    [[nodiscard]] constexpr
    auto operator->() const noexcept -> S const* requires(not void_success)
    { assert_object(has_value(), "expected::operator-> called without a value"); return &mValue; }

    constexpr auto value() const& -> void { check_object(); }
    constexpr auto value() & -> void { check_object(); }
    constexpr auto value() const&& -> void { check_object(); }
    constexpr auto value() && -> void { check_object(); }

    [[nodiscard]] constexpr
    auto value() & -> S& requires(not void_success) { check_object(); return mValue; }
    [[nodiscard]] constexpr
    auto value() const& -> const S& requires(not void_success) { check_object(); return mValue; }
    [[nodiscard]] constexpr
    auto value() && -> S&& requires(not void_success)
    { check_object(); return std::move(mValue); }
    [[nodiscard]] constexpr
    auto value() const&& -> const S&& requires(not void_success)
    { check_object(); return std::move(mValue); }

    template<typename U> [[nodiscard]] constexpr
    auto value_or(U&& defval) const& -> S requires(not void_success)
    { return bool{*this} ? **this : static_cast<S>(std::forward<U>(defval)); }
    template<typename U> [[nodiscard]] constexpr
    auto value_or(U&& defval) && -> S requires(not void_success)
    { return bool{*this} ? std::move(**this) : static_cast<S>(std::forward<U>(defval)); }

    [[nodiscard]] constexpr
    auto error() & noexcept -> Er&
    { assert_object(not has_value(), "expected::error called without an error"); return mError; }
    [[nodiscard]] constexpr
    auto error() const& noexcept -> const Er&
    { assert_object(not has_value(), "expected::error called without an error"); return mError; }
    [[nodiscard]] constexpr
    auto error() && noexcept -> Er&&
    {
        assert_object(not has_value(), "expected::error called without an error");
        return std::move(mError);
    }
    [[nodiscard]] constexpr
    auto error() const&& noexcept -> const Er&&
    {
        assert_object(not has_value(), "expected::error called without an error");
        return std::move(mError);
    }
#undef assert_object

    template<typename F> [[nodiscard]] constexpr
    auto and_then(F&& fn) &
    {
        using ret_t = std::remove_cvref_t<std::invoke_result_t<F&&, Ty&>>;
        if(has_value())
            return std::invoke(std::forward<F>(fn), mValue);
        return ret_t{unexpect, mError};
    }
    template<typename F> [[nodiscard]] constexpr
    auto and_then(F&& fn) const&
    {
        using ret_t = std::remove_cvref_t<std::invoke_result_t<F&&, Ty const&>>;
        if(has_value())
            return std::invoke(std::forward<F>(fn), mValue);
        return ret_t{unexpect, mError};
    }
    template<typename F> [[nodiscard]] constexpr
    auto and_then(F&& fn) &&
    {
        using ret_t = std::remove_cvref_t<std::invoke_result_t<F&&, Ty&&>>;
        if(has_value())
            return std::invoke(std::forward<F>(fn), std::move(mValue));
        return ret_t{unexpect, std::move(mError)};
    }
    template<typename F> [[nodiscard]] constexpr
    auto and_then(F&& fn) const&&
    {
        using ret_t = std::remove_cvref_t<std::invoke_result_t<F&&, Ty const&&>>;
        if(has_value())
            return std::invoke(std::forward<F>(fn), std::move(mValue));
        return ret_t{unexpect, std::move(mError)};
    }

    template<typename F> [[nodiscard]] constexpr
    auto transform(F&& fn) &
    {
        using Ty2 = std::remove_cv_t<std::invoke_result_t<F&&, Ty&>>;
        using ret_t = expected<Ty2, Er>;
        if(has_value())
        {
            if constexpr(void_success)
                return ret_t{in_place_inv_{}, std::forward<F>(fn)};
            else
                return ret_t{in_place_inv_{},
                    [&]{ return std::invoke(std::forward<F>(fn), mValue); }};
        }
        return ret_t{unexpect, mError};
    }
    template<typename F> [[nodiscard]] constexpr
    auto transform(F&& fn) const&
    {
        using Ty2 = std::remove_cv_t<std::invoke_result_t<F&&, Ty const&>>;
        using ret_t = expected<Ty2, Er>;
        if(has_value())
        {
            if constexpr(void_success)
                return ret_t{in_place_inv_{}, std::forward<F>(fn)};
            else
                return ret_t{in_place_inv_{},
                    [&]{ return std::invoke(std::forward<F>(fn), mValue); }};
        }
        return ret_t{unexpect, mError};
    }
    template<typename F> [[nodiscard]] constexpr
    auto transform(F&& fn) &&
    {
        using Ty2 = std::remove_cv_t<std::invoke_result_t<F&&, Ty&&>>;
        using ret_t = expected<Ty2, Er>;
        if(has_value())
        {
            if constexpr(void_success)
                return ret_t{in_place_inv_{}, std::forward<F>(fn)};
            else
                return ret_t{in_place_inv_{},
                    [&]{ return std::invoke(std::forward<F>(fn), std::move(mValue)); }};
        }
        return ret_t{unexpect, std::move(mError)};
    }
    template<typename F> [[nodiscard]] constexpr
    auto transform(F&& fn) const&&
    {
        using Ty2 = std::remove_cv_t<std::invoke_result_t<F&&, Ty const&&>>;
        using ret_t = expected<Ty2, Er>;
        if(has_value())
        {
            if constexpr(void_success)
                return ret_t{in_place_inv_{}, std::forward<F>(fn)};
            else
                return ret_t{in_place_inv_{},
                    [&]{ return std::invoke(std::forward<F>(fn), std::move(mValue)); }};
        }
        return ret_t{unexpect, std::move(mError)};
    }

    template<typename F> [[nodiscard]] constexpr
    auto or_else(F&& fn) &
    {
        using ret_t = std::remove_cvref_t<std::invoke_result_t<F&&, Er&>>;
        if(has_value())
            return ret_t{std::in_place, mValue};
        return std::invoke(std::forward<F>(fn), mError);
    }
    template<typename F> [[nodiscard]] constexpr
    auto or_else(F&& fn) const&
    {
        using ret_t = std::remove_cvref_t<std::invoke_result_t<F&&, Er const&>>;
        if(has_value())
            return ret_t{std::in_place, mValue};
        return std::invoke(std::forward<F>(fn), mError);
    }
    template<typename F> [[nodiscard]] constexpr
    auto or_else(F&& fn) &&
    {
        using ret_t = std::remove_cvref_t<std::invoke_result_t<F&&, Er&&>>;
        if(has_value())
            return ret_t{std::in_place, std::move(mValue)};
        return std::invoke(std::forward<F>(fn), std::move(mError));
    }
    template<typename F> [[nodiscard]] constexpr
    auto or_else(F&& fn) const&&
    {
        using ret_t = std::remove_cvref_t<std::invoke_result_t<F&&, Er const&&>>;
        if(has_value())
            return ret_t{std::in_place, std::move(mValue)};
        return std::invoke(std::forward<F>(fn), std::move(mError));
    }

    template<typename F> [[nodiscard]] constexpr
    auto transform_error(F&& fn) &
    {
        using Er2 = std::remove_cv_t<std::invoke_result_t<F&&, Er&>>;
        using ret_t = expected<Ty, Er2>;
        if(has_value())
        {
            if constexpr(void_success)
                return ret_t{std::in_place};
            else
                return ret_t{std::in_place, mValue};
        }
        return ret_t{unexpect_inv_{}, [&]{ return std::invoke(std::forward<F>(fn), mError); }};
    }
    template<typename F> [[nodiscard]] constexpr
    auto transform_error(F&& fn) const&
    {
        using Er2 = std::remove_cv_t<std::invoke_result_t<F&&, Er const&>>;
        using ret_t = expected<Ty, Er2>;
        if(has_value())
        {
            if constexpr(void_success)
                return ret_t{std::in_place};
            else
                return ret_t{std::in_place, mValue};
        }
        return ret_t{unexpect_inv_{}, [&]{ return std::invoke(std::forward<F>(fn), mError); }};
    }
    template<typename F> [[nodiscard]] constexpr
    auto transform_error(F&& fn) &&
    {
        using Er2 = std::remove_cv_t<std::invoke_result_t<F&&, Er&&>>;
        using ret_t = expected<Ty, Er2>;
        if(has_value())
        {
            if constexpr(void_success)
                return ret_t{std::in_place};
            else
                return ret_t{std::in_place, std::move(mValue)};
        }
        return ret_t{unexpect_inv_{},
            [&]{ return std::invoke(std::forward<F>(fn), std::move(mError)); }};
    }
    template<typename F> [[nodiscard]] constexpr
    auto transform_error(F&& fn) const&&
    {
        using Er2 = std::remove_cv_t<std::invoke_result_t<F&&, Er const&&>>;
        using ret_t = expected<Ty, Er2>;
        if(has_value())
        {
            if constexpr(void_success)
                return ret_t{std::in_place};
            else
                return ret_t{std::in_place, std::move(mValue)};
        }
        return ret_t{unexpect_inv_{},
            [&]{ return std::invoke(std::forward<F>(fn), std::move(mError)); }};
    }
    /* NOLINTEND(cppcoreguidelines-pro-type-union-access) */

    template<typename Ty2, typename Er2> requires(not void_success and not std::is_void_v<Ty2>)
        friend constexpr
    auto operator==(expected const &lhs, expected<Ty2, Er2> const &rhs) noexcept -> bool
    {
        return lhs.has_value() != rhs.has_value() ? false
            : (lhs.has_value() ? *lhs == *rhs : lhs.error() == rhs.error());
    }

    template<typename Ty2, typename Er2> requires(void_success and std::is_void_v<Ty2>) friend
        constexpr
    auto operator==(expected const &lhs, expected<Ty2, Er2> const &rhs) noexcept -> bool
    {
        return lhs.has_value() != rhs.has_value() ? false
            : (lhs.has_value() or lhs.error() == rhs.error());
    }

    template<typename Er2> friend constexpr
    auto operator==(expected const &lhs, unexpected<Er2> const &unex) noexcept -> bool
    { return not lhs.has_value() and lhs.error() == unex.error(); }

    template<class Ty2> requires(not void_success) friend constexpr
    auto operator==(expected const &lhs, Ty2 const &val) noexcept -> bool
    { return lhs.has_value() and *lhs == val; }
};

} /* namespace al */

#endif /* AL_EXPECTED_HPP */
