#ifndef AL_MALLOC_H
#define AL_MALLOC_H

#include <algorithm>
#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>
#include <variant>

#include "gsl/gsl"


#define DISABLE_ALLOC                                                         \
    auto operator new(std::size_t) -> void* = delete;                         \
    auto operator new[](std::size_t) -> void* = delete;                       \
    auto operator delete(void*) noexcept -> void = delete;                    \
    auto operator delete[](void*) noexcept -> void = delete;

namespace al {

template<typename T, std::size_t AlignV=alignof(T)>
struct allocator {
    static constexpr auto Alignment = std::max(AlignV, alignof(T));
    static constexpr auto AlignVal = std::align_val_t{Alignment};

    using value_type = std::remove_cvref_t<T>;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using is_always_equal = std::true_type;

    template<typename U> requires(alignof(U) <= Alignment)
    struct rebind {
        using other = allocator<U, Alignment>;
    };

    constexpr explicit allocator() noexcept = default;
    template<typename U, std::size_t N>
    constexpr explicit allocator(const allocator<U,N>&) noexcept
    { static_assert(Alignment == allocator<U,N>::Alignment); }

    static constexpr auto allocate(std::size_t const n) -> gsl::owner<T*>
    {
        if(n > std::numeric_limits<std::size_t>::max()/sizeof(T)) throw std::bad_alloc();
        return static_cast<gsl::owner<T*>>(::operator new[](n*sizeof(T), AlignVal));
    }
    static constexpr void deallocate(gsl::owner<T*> const p, std::size_t) noexcept
    { ::operator delete[](gsl::owner<void*>{p}, AlignVal); }

    template<typename U, std::size_t M> [[nodiscard]] friend constexpr
    auto operator==(const allocator&, const allocator<U,M>&) noexcept -> bool
    { return Alignment == allocator<U,M>::Alignment; }
};


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

template<typename SP, typename PT, typename ...Args> requires(not std::is_same_v<PT, void*>)
class out_ptr_t {
    SP &mRes;
    std::variant<PT, void*> mPtr;
    [[no_unique_address]] std::tuple<Args...> mArgs;

    static_assert(detail::can_reset<SP, PT, Args...> or detail::can_assign<SP, PT, Args...>,
        "Smart pointer type can't be reset or assigned with the given argument types");

    constexpr auto finish(PT const ptr) -> decltype(auto)
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
        : mRes{res}, mPtr{PT{}}, mArgs{std::forward<Args>(args)...}
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
    { return &std::get<PT>(mPtr); }

    constexpr operator void**() noexcept /* NOLINT(google-explicit-constructor) */
    { return &mPtr.template emplace<void*>(); }
};

template<typename T=void, typename SP, typename ...Args> [[nodiscard]] constexpr
auto out_ptr(SP &res, Args&& ...args)
{
    using ptr_t = decltype(detail::pointer_type_for<T, SP>());
    static_assert(not std::is_void_v<ptr_t>);
    if constexpr(not std::is_void_v<ptr_t>)
        return out_ptr_t<SP, ptr_t, Args&&...>{res, std::forward<Args>(args)...};
}


template<typename SP, typename PT, typename ...Args> requires(not std::is_same_v<PT, void*>)
class inout_ptr_t {
    SP &mRes;
    std::variant<PT, void*> mPtr;
    [[no_unique_address]] std::tuple<Args...> mArgs;

    static_assert(detail::can_reset<SP, PT, Args...> or detail::can_assign<SP, PT, Args...>,
        "Smart pointer type can't be reset or assigned with the given argument types");

    constexpr auto finish(PT const ptr) -> decltype(auto)
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
        : mRes{res}, mPtr{res.get()}, mArgs{std::forward<Args>(args)...}
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
    { return &std::get<PT>(mPtr); }

    constexpr operator void**() noexcept /* NOLINT(google-explicit-constructor) */
    { return &mPtr.template emplace<void*>(mRes.get()); }
};

template<typename T=void, typename SP, typename ...Args> [[nodiscard]] constexpr
auto inout_ptr(SP &res, Args&& ...args)
{
    using ptr_t = decltype(detail::pointer_type_for<T, SP>());
    static_assert(not std::is_void_v<ptr_t>);
    if constexpr(not std::is_void_v<ptr_t>)
        return inout_ptr_t<SP, ptr_t, Args&&...>{res, std::forward<Args>(args)...};
}

} // namespace al

#endif /* AL_MALLOC_H */
