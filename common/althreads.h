#ifndef AL_THREADS_H
#define AL_THREADS_H

#include <cstdint>
#include <stdexcept>
#include <type_traits>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#else

#include <threads.h>
#endif

#include "albit.h"

namespace al {

template<typename T>
class tss {
    static_assert(sizeof(T) <= sizeof(void*));
    static_assert(std::is_trivially_destructible_v<T> && std::is_trivially_copy_constructible_v<T>);

    static void *as_ptr(const T &value) noexcept
    {
        if constexpr(std::is_pointer_v<T>)
        {
            if constexpr(std::is_const_v<std::remove_pointer_t<T>>)
                return const_cast<void*>(static_cast<const void*>(value)); /* NOLINT(*-const-cast) */
            else
                return static_cast<void*>(value);
        }
        else if constexpr(sizeof(T) == sizeof(void*))
            return al::bit_cast<void*>(value);
        else if constexpr(std::is_integral_v<T>)
            return al::bit_cast<void*>(static_cast<std::uintptr_t>(value));
    }

#ifdef _WIN32
    DWORD mTss;

public:
    tss()
    {
        mTss = TlsAlloc();
        if(mTss == TLS_OUT_OF_INDEXES)
            throw std::runtime_error{"al::tss::tss()"};
    }
    explicit tss(const T &init) : tss{}
    {
        if(TlsSetValue(mTss, as_ptr(init)) == FALSE)
            throw std::runtime_error{"al::tss::tss(T)"};
    }
    tss(const tss&) = delete;
    tss(tss&&) = delete;
    ~tss() { TlsFree(mTss); }

    void operator=(const tss&) = delete;
    void operator=(tss&&) = delete;

    void set(const T &value)
    {
        if(TlsSetValue(mTss, as_ptr(value)) == FALSE)
            throw std::runtime_error{"al::tss::set(T)"};
    }

    [[nodiscard]]
    auto get() const noexcept -> T
    {
        void *res{TlsGetValue(mTss)};
        if constexpr(std::is_pointer_v<T>)
            return static_cast<T>(res);
        else if constexpr(sizeof(T) == sizeof(void*))
            return al::bit_cast<T>(res);
        else if constexpr(std::is_integral_v<T>)
            return static_cast<T>(al::bit_cast<std::uintptr_t>(res));
    }

#else

    tss_t mTss;

public:
    tss()
    {
        if(int res{tss_create(&mTss, nullptr)}; res != thrd_success)
            throw std::runtime_error{"al::tss::tss()"};
    }
    explicit tss(const T &init) : tss{}
    {
        if(int res{tss_set(mTss, as_ptr(init))}; res != thrd_success)
            throw std::runtime_error{"al::tss::tss(T)"};
    }
    tss(const tss&) = delete;
    tss(tss&&) = delete;
    ~tss() { tss_delete(mTss); }

    void operator=(const tss&) = delete;
    void operator=(tss&&) = delete;

    void set(const T &value)
    {
        if(int res{tss_set(mTss, as_ptr(value))}; res != thrd_success)
            throw std::runtime_error{"al::tss::set(T)"};
    }

    [[nodiscard]]
    auto get() const noexcept -> T
    {
        void *res{tss_get(mTss)};
        if constexpr(std::is_pointer_v<T>)
            return static_cast<T>(res);
        else if constexpr(sizeof(T) == sizeof(void*))
            return al::bit_cast<T>(res);
        else if constexpr(std::is_integral_v<T>)
            return static_cast<T>(al::bit_cast<std::uintptr_t>(res));
    }
#endif /* _WIN32 */
};

} // namespace al

#endif /* AL_THREADS_H */
