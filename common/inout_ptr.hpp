#ifndef INOUT_PTR_HPP
#define INOUT_PTR_HPP

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>


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
    std::variant<PT, void*> mPtr;
    [[no_unique_address]] std::tuple<Args...> mArgs;

    static_assert(detail::can_reset<SP, PT, Args...> or detail::can_assign<SP, PT, Args...>,
        "Smart pointer type can't be reset or assigned with the given argument types");

    constexpr auto finish(PT const& ptr) -> decltype(auto)
    {
        return std::apply([&]<typename ...Args_>(Args_&& ...args) -> decltype(auto)
        {
            if constexpr(detail::can_reset<SP, PT, Args...>)
                return mRes.reset(ptr, std::forward<Args_>(args)...);
            else if constexpr(detail::can_assign<SP, PT, Args...>)
                return mRes = SP(ptr, std::forward<Args_>(args)...);
        }, std::move(mArgs));
    }

public:
    constexpr explicit
    out_ptr_t(SP &res, Args ...args)
        : mRes{res}, mPtr{std::in_place_index<0>, PT{}}, mArgs{std::forward<Args>(args)...}
    {
        if constexpr(requires { mRes.reset(); })
            mRes.reset();
        else
            mRes = SP();
    }

    constexpr
    ~out_ptr_t() { std::visit([&](auto &ptr) { finish(static_cast<PT>(ptr)); }, mPtr); }

    out_ptr_t() = delete;
    out_ptr_t(const out_ptr_t&) = delete;
    out_ptr_t& operator=(const out_ptr_t&) = delete;

    constexpr operator PT*() noexcept /* NOLINT(google-explicit-constructor) */
    { return &std::get<0>(mPtr); }

    constexpr operator void**() noexcept /* NOLINT(google-explicit-constructor) */
    { return &mPtr.template emplace<1>(); }
};

template<typename T=void, typename SP, typename ...Args> [[nodiscard]] constexpr
auto out_ptr(SP &res, Args&& ...args)
{
    using ptr_t = decltype(detail::pointer_type_for<T, SP>());
    static_assert(not std::is_void_v<ptr_t>);
    if constexpr(not std::is_void_v<ptr_t>)
        return out_ptr_t<SP, ptr_t, Args&&...>{res, std::forward<Args>(args)...};
}


template<typename SP, typename PT, typename ...Args>
class inout_ptr_t {
    SP &mRes;
    std::variant<PT, void*> mPtr;
    [[no_unique_address]] std::tuple<Args...> mArgs;

    static_assert(detail::can_reset<SP, PT, Args...> or detail::can_assign<SP, PT, Args...>,
        "Smart pointer type can't be reset or assigned with the given argument types");

    constexpr auto finish(PT const& ptr) -> decltype(auto)
    {
        return std::apply([&]<typename ...Args_>(Args_&& ...args) -> decltype(auto)
        {
            if constexpr(detail::can_reset<SP, PT, Args...>)
                return mRes.reset(ptr, std::forward<Args_>(args)...);
            else if constexpr(detail::can_assign<SP, PT, Args...>)
                return mRes = SP(ptr, std::forward<Args_>(args)...);
        }, std::move(mArgs));
    }

public:
    constexpr explicit
    inout_ptr_t(SP &res, Args ...args)
        : mRes{res}, mPtr{std::in_place_index<0>, res.get()}, mArgs{std::forward<Args>(args)...}
    { }
    constexpr ~inout_ptr_t()
    {
        mRes.release();
        std::visit([&](auto &ptr) { finish(static_cast<PT>(ptr)); }, mPtr);
    }

    inout_ptr_t() = delete;
    inout_ptr_t(const inout_ptr_t&) = delete;
    inout_ptr_t& operator=(const inout_ptr_t&) = delete;

    constexpr operator PT*() noexcept /* NOLINT(google-explicit-constructor) */
    { return &std::get<0>(mPtr); }

    constexpr operator void**() noexcept /* NOLINT(google-explicit-constructor) */
    { return &mPtr.template emplace<1>(mRes.get()); }
};

template<typename T=void, typename SP, typename ...Args> [[nodiscard]] constexpr
auto inout_ptr(SP &res, Args&& ...args)
{
    using ptr_t = decltype(detail::pointer_type_for<T, SP>());
    static_assert(not std::is_void_v<ptr_t>);
    if constexpr(not std::is_void_v<ptr_t>)
        return inout_ptr_t<SP, ptr_t, Args&&...>{res, std::forward<Args>(args)...};
}

}

#endif /* INOUT_PTR_HPP */
