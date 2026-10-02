#ifndef INOUT_PTR_HPP
#define INOUT_PTR_HPP

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "instance_of.hpp"


namespace al {

namespace detail {

    template<typename T, typename ...Args>
    concept can_reset = requires(T &sp) { sp.reset(std::declval<Args>()...); };

    template<typename T, typename ...Args>
    concept can_assign = requires(T &sp) { sp = T(std::declval<Args>()...); };

    template<typename PT, typename SP> consteval
    auto pointer_type_for() noexcept
    {
        if constexpr(not std::is_void_v<PT>)
        {
            static_assert(std::is_pointer_v<PT>);
            return std::type_identity_t<PT>();
        }
        else if constexpr(requires { typename SP::pointer; })
            return std::type_identity_t<typename SP::pointer>();
        else if constexpr(requires { typename SP::element_type; })
            return std::type_identity_t<typename SP::element_type*>();
        else if constexpr(requires { typename std::pointer_traits<SP>::element_type; })
            return std::type_identity_t<typename std::pointer_traits<SP>::element_type*>();
        else
            static_assert(not std::is_void_v<PT>
                or requires { typename SP::pointer; }
                or requires { typename SP::element_type; }
                or requires { typename std::pointer_traits<SP>::element_type; });
    }

}

template<typename SP, typename PT, typename ...Args>
class out_ptr_t {
    SP &mRes;
    PT mPtr;
    [[no_unique_address]] std::tuple<Args...> mArgs;

    static_assert(detail::can_reset<SP, PT, Args...> or detail::can_assign<SP, PT, Args...>,
        "Smart pointer type can't be reset or assigned with the given argument types");

    static_assert(not instance_of<SP, std::shared_ptr> or sizeof...(Args) > 0,
        "std::shared_ptr must have a deleter argument");

public:
    constexpr explicit
    out_ptr_t(SP &res, Args ...args) : mRes{res}, mPtr{}, mArgs{std::forward<Args>(args)...}
    {
        if constexpr(requires { mRes.reset(); })
            mRes.reset();
        else
            mRes = SP();
    }

    constexpr
    ~out_ptr_t()
    {
        std::apply([&]<typename ...Args_>(Args_&& ...args)
        {
            if constexpr(detail::can_reset<SP, PT, Args...>)
                mRes.reset(mPtr, std::forward<Args_>(args)...);
            else if constexpr(detail::can_assign<SP, PT, Args...>)
                mRes = SP(mPtr, std::forward<Args_>(args)...);
        }, std::move(mArgs));
    }

    out_ptr_t() = delete;
    out_ptr_t(const out_ptr_t&) = delete;
    out_ptr_t& operator=(const out_ptr_t&) = delete;

    explicit(false) constexpr /* NOLINTNEXTLINE(google-explicit-constructor) */
    operator PT*() noexcept { return &mPtr; }

    explicit(false) constexpr /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, google-explicit-constructor) */
    operator void**() noexcept { return reinterpret_cast<void**>(&mPtr); }
};

template<typename T=void, typename SP, typename ...Args> [[nodiscard]] constexpr
auto out_ptr(SP &res, Args&& ...args)
{
    using ptr_t = decltype(detail::pointer_type_for<T, SP>());
    if constexpr(not std::is_void_v<ptr_t>)
        return out_ptr_t<SP, ptr_t, Args&&...>{res, std::forward<Args>(args)...};
    else
        static_assert(not std::is_void_v<ptr_t>);
}


template<typename SP, typename PT, typename ...Args>
class inout_ptr_t {
    SP &mRes;
    PT mPtr;
    [[no_unique_address]] std::tuple<Args...> mArgs;

    static_assert(detail::can_reset<SP, PT, Args...> or detail::can_assign<SP, PT, Args...>,
        "Smart pointer type can't be reset or assigned with the given argument types");

    static_assert(not instance_of<SP, std::shared_ptr> or sizeof...(Args) > 0,
        "std::shared_ptr must have a deleter argument");

public:
    constexpr explicit
    inout_ptr_t(SP &res, Args ...args)
        : mRes{res}, mPtr{mRes.release()}, mArgs{std::forward<Args>(args)...}
    { }
    constexpr ~inout_ptr_t()
    {
        std::apply([&]<typename ...Args_>(Args_&& ...args)
        {
            if constexpr(detail::can_reset<SP, PT, Args...>)
                mRes.reset(mPtr, std::forward<Args_>(args)...);
            else if constexpr(detail::can_assign<SP, PT, Args...>)
                mRes = SP(mPtr, std::forward<Args_>(args)...);
        }, std::move(mArgs));
    }

    inout_ptr_t() = delete;
    inout_ptr_t(const inout_ptr_t&) = delete;
    inout_ptr_t& operator=(const inout_ptr_t&) = delete;

    explicit(false) constexpr /* NOLINTNEXTLINE(google-explicit-constructor) */
    operator PT*() noexcept { return &mPtr; }

    explicit(false) constexpr /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, google-explicit-constructor) */
    operator void**() noexcept { return reinterpret_cast<void**>(&mPtr); }
};

template<typename T=void, typename SP, typename ...Args> [[nodiscard]] constexpr
auto inout_ptr(SP &res, Args&& ...args)
{
    using ptr_t = decltype(detail::pointer_type_for<T, SP>());
    if constexpr(not std::is_void_v<ptr_t>)
        return inout_ptr_t<SP, ptr_t, Args&&...>{res, std::forward<Args>(args)...};
    else
        static_assert(not std::is_void_v<ptr_t>);
}

}

#endif /* INOUT_PTR_HPP */
