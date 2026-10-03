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

    template<typename SP, typename PT=void> consteval
    auto pointer_of() noexcept
    {
        if constexpr(requires { typename SP::pointer; })
            return std::type_identity_t<typename SP::pointer>();
        else if constexpr(requires { typename SP::element_type; })
            return std::type_identity_t<typename SP::element_type*>();
        else if constexpr(requires { typename std::pointer_traits<SP>::element_type; })
            return std::type_identity_t<typename std::pointer_traits<SP>::element_type*>();
        else if constexpr(not std::is_void_v<PT>)
            return std::type_identity_t<PT>();
        else
            static_assert(requires { typename SP::pointer; }
                or requires { typename SP::element_type; }
                or requires { typename std::pointer_traits<SP>::element_type; });
    }

}

#define POINTER_OF(SP) decltype(detail::pointer_of<SP>())

#define POINTER_OF_OR(SP, PT) decltype(detail::pointer_of<SP, PT>())


template<typename SP, typename PT, typename ...Args>
class out_ptr_t {
    SP &mRes;
    [[no_unique_address]] PT mPtr;
    [[no_unique_address]] std::tuple<Args...> mArgs;

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
        /* Don't double-reset with a null result. */
        if(not mPtr)
            return;

        std::apply([&]<typename ...Args_>(Args_&& ...args)
        {
            /* The "real" pointer type wanted by the smart pointer container. */
            using ptr_t = POINTER_OF_OR(SP, PT);

            if constexpr(detail::can_reset<SP, ptr_t, Args...>)
                mRes.reset(static_cast<ptr_t>(mPtr), std::forward<Args_>(args)...);
            else if constexpr(detail::can_assign<SP, ptr_t, Args...>)
                mRes = SP(static_cast<ptr_t>(mPtr), std::forward<Args_>(args)...);
            else
                static_assert(detail::can_reset<SP, ptr_t, Args...>
                    or detail::can_assign<SP, ptr_t, Args...>,
                    "Smart pointer type can't be reset or assigned with the given argument types");
        }, std::move(mArgs));
    }

    out_ptr_t() = delete;
    out_ptr_t(const out_ptr_t&) = delete;
    out_ptr_t& operator=(const out_ptr_t&) = delete;

    explicit(false) constexpr /* NOLINTNEXTLINE(google-explicit-constructor) */
    operator PT*() && noexcept { return std::addressof(mPtr); }

    explicit(false) /* NOLINTNEXTLINE(google-explicit-constructor) */
    operator void**() && noexcept requires(not std::same_as<PT,void*>)
    {
        static_assert(std::is_pointer_v<PT>,
            "The pointer type must be void* compatible for operator void**");
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
        return reinterpret_cast<void**>(static_cast<PT*>(std::move(*this)));
    }
};

template<typename T=void, typename SP, typename ...Args> [[nodiscard]] constexpr
auto out_ptr(SP &res, Args&& ...args)
{
    using ptr_t = std::conditional_t<std::is_void_v<T>, POINTER_OF(SP), T>;
    if constexpr(not std::is_void_v<ptr_t>)
        return out_ptr_t<SP, ptr_t, Args&&...>{res, std::forward<Args>(args)...};
}


template<typename SP, typename PT, typename ...Args>
class inout_ptr_t {
    SP &mRes;
    [[no_unique_address]] PT mPtr;
    [[no_unique_address]] std::tuple<Args...> mArgs;

    static_assert(not instance_of<SP, std::shared_ptr>,
        "std::shared_ptr cannot be used with inout_ptr");

public:
    constexpr explicit
    inout_ptr_t(SP &res, Args ...args)
        : mRes{res}, mPtr{std::invoke([&res] {
            if constexpr(requires { res.release(); })
                return res.release();
            else
            {
                static_assert(std::is_pointer_v<SP>);
                return std::exchange(res, nullptr);
            }
        })}
        , mArgs{std::forward<Args>(args)...}
    { }
    constexpr
    ~inout_ptr_t()
    {
        /* Don't reset-after-release with a null result. */
        if(not mPtr)
            return;

        std::apply([&]<typename ...Args_>(Args_&& ...args)
        {
            /* The "real" pointer type wanted by the smart pointer container. */
            using ptr_t = POINTER_OF_OR(SP, PT);

            if constexpr(detail::can_reset<SP, ptr_t, Args...>)
                mRes.reset(static_cast<ptr_t>(mPtr), std::forward<Args_>(args)...);
            else if constexpr(detail::can_assign<SP, ptr_t, Args...>)
                mRes = SP(static_cast<ptr_t>(mPtr), std::forward<Args_>(args)...);
            else
                static_assert(detail::can_reset<SP, ptr_t, Args...>
                    or detail::can_assign<SP, ptr_t, Args...>,
                    "Smart pointer type can't be reset or assigned with the given argument types");
        }, std::move(mArgs));
    }

    inout_ptr_t() = delete;
    inout_ptr_t(const inout_ptr_t&) = delete;
    inout_ptr_t& operator=(const inout_ptr_t&) = delete;

    explicit(false) constexpr /* NOLINTNEXTLINE(google-explicit-constructor) */
    operator PT*() && noexcept { return std::addressof(mPtr); }

    explicit(false) /* NOLINTNEXTLINE(google-explicit-constructor) */
    operator void**() && noexcept requires(not std::same_as<PT,void*>)
    {
        static_assert(std::is_pointer_v<PT>,
            "The pointer type must be void* compatible for operator void**");
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
        return reinterpret_cast<void**>(static_cast<PT*>(std::move(*this)));
    }
};

template<typename T=void, typename SP, typename ...Args> [[nodiscard]] constexpr
auto inout_ptr(SP &res, Args&& ...args)
{
    using ptr_t = std::conditional_t<std::is_void_v<T>, POINTER_OF(SP), T>;
    if constexpr(not std::is_void_v<ptr_t>)
        return inout_ptr_t<SP, ptr_t, Args&&...>{res, std::forward<Args>(args)...};
}

}

#endif /* INOUT_PTR_HPP */
