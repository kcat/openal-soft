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
    void *operator new(size_t) = delete;                                      \
    void *operator new[](size_t) = delete;                                    \
    void operator delete(void*) noexcept = delete;                            \
    void operator delete[](void*) noexcept = delete;


enum FamCount : size_t { };

#define DEF_FAM_NEWDEL(T, FamMem)                                             \
    static constexpr size_t Sizeof(size_t count) noexcept                     \
    {                                                                         \
        static_assert(&Sizeof == &T::Sizeof,                                  \
            "Incorrect container type specified");                            \
        return std::max(decltype(FamMem)::Sizeof(count, offsetof(T, FamMem)), \
            sizeof(T));                                                       \
    }                                                                         \
                                                                              \
    gsl::owner<void*> operator new(size_t /*size*/, FamCount count)           \
    {                                                                         \
        const auto alignment = std::align_val_t{alignof(T)};                  \
        return ::operator new[](T::Sizeof(count), alignment);                 \
    }                                                                         \
    void operator delete(gsl::owner<void*> block, FamCount) noexcept          \
    { ::operator delete[](block, std::align_val_t{alignof(T)}); }             \
    void operator delete(gsl::owner<void*> block) noexcept                    \
    { ::operator delete[](block, std::align_val_t{alignof(T)}); }             \
    void *operator new[](size_t /*size*/) = delete;                           \
    void operator delete[](void* /*block*/) = delete;


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
        using other = allocator<U,Alignment>;
    };

    constexpr explicit allocator() noexcept = default;
    template<typename U, std::size_t N>
    constexpr explicit allocator(const allocator<U,N>&) noexcept
    { static_assert(Alignment == allocator<U,N>::Alignment); }

    static constexpr auto allocate(std::size_t n) -> gsl::owner<T*>
    {
        if(n > std::numeric_limits<std::size_t>::max()/sizeof(T)) throw std::bad_alloc();
        return static_cast<gsl::owner<T*>>(::operator new[](n*sizeof(T), AlignVal));
    }
    static constexpr void deallocate(gsl::owner<T*> p, std::size_t) noexcept
    { ::operator delete[](gsl::owner<void*>{p}, AlignVal); }
};
template<typename T, std::size_t N, typename U, std::size_t M>
constexpr bool operator==(const allocator<T,N>&, const allocator<U,M>&) noexcept
{ return allocator<T,N>::Alignment == allocator<U,M>::Alignment; }
template<typename T, std::size_t N, typename U, std::size_t M>
constexpr bool operator!=(const allocator<T,N>&, const allocator<U,M>&) noexcept
{ return allocator<T,N>::Alignment != allocator<U,M>::Alignment; }


template<typename SP, typename PT, typename...>
class out_ptr_t {
    static_assert(!std::is_same_v<PT,void*>);

    SP &mRes;
    std::variant<PT,void*> mPtr;

public:
    explicit out_ptr_t(SP &res) : mRes{res} { }
    ~out_ptr_t() { std::visit([this](auto &ptr) { mRes.reset(static_cast<PT>(ptr)); }, mPtr); }

    out_ptr_t() = delete;
    out_ptr_t(const out_ptr_t&) = delete;
    out_ptr_t& operator=(const out_ptr_t&) = delete;

    operator PT*() noexcept /* NOLINT(google-explicit-constructor) */
    { return &std::get<PT>(mPtr); }

    operator void**() noexcept /* NOLINT(google-explicit-constructor) */
    { return &mPtr.template emplace<void*>(); }
};

template<typename T=void, typename SP, typename ...Args>
auto out_ptr(SP &res, Args&& ...args)
{
    static_assert(sizeof...(args) == 0);
    if constexpr(std::is_same_v<T,void>)
    {
        using ptype = SP::element_type*;
        return out_ptr_t<SP,ptype,Args...>{res};
    }
    else
        return out_ptr_t<SP,T,Args...>{res};
}


template<typename SP, typename PT, typename...>
class inout_ptr_t {
    static_assert(!std::is_same_v<PT,void*>);

    SP &mRes;
    std::variant<PT,void*> mPtr;

public:
    explicit inout_ptr_t(SP &res) : mRes{res}, mPtr{res.get()} { }
    ~inout_ptr_t()
    {
        mRes.release();
        std::visit([this](auto &ptr) { mRes.reset(static_cast<PT>(ptr)); }, mPtr);
    }

    inout_ptr_t() = delete;
    inout_ptr_t(const inout_ptr_t&) = delete;
    inout_ptr_t& operator=(const inout_ptr_t&) = delete;

    operator PT*() noexcept /* NOLINT(google-explicit-constructor) */
    { return &std::get<PT>(mPtr); }

    operator void**() noexcept /* NOLINT(google-explicit-constructor) */
    { return &mPtr.template emplace<void*>(mRes.get()); }
};

template<typename T=void, typename SP, typename ...Args>
auto inout_ptr(SP &res, Args&& ...args)
{
    static_assert(sizeof...(args) == 0);
    if constexpr(std::is_same_v<T,void>)
    {
        using ptype = SP::element_type*;
        return inout_ptr_t<SP,ptype,Args...>{res};
    }
    else
        return inout_ptr_t<SP,T,Args...>{res};
}

} // namespace al

#endif /* AL_MALLOC_H */
