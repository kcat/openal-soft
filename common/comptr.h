#ifndef COMMON_COMPTR_H
#define COMMON_COMPTR_H

#ifdef _WIN32
#include <cstddef>
#include <utility>

#include <windows.h>
#include <objbase.h>

class ComWrapper {
    HRESULT mStatus{};

public:
    ComWrapper(void *const reserved, DWORD const coinit)
        : mStatus{CoInitializeEx(reserved, coinit)}
    { }
    explicit ComWrapper(DWORD const coinit=COINIT_APARTMENTTHREADED)
        : mStatus{CoInitializeEx(nullptr, coinit)}
    { }
    ComWrapper(ComWrapper&& rhs) noexcept { mStatus = std::exchange(rhs.mStatus, E_FAIL); }
    ComWrapper(const ComWrapper&) = delete;
    ~ComWrapper() { if(SUCCEEDED(mStatus)) CoUninitialize(); }

    auto operator=(ComWrapper&& rhs) noexcept -> ComWrapper&
    {
        if(this != &rhs) [[likely]]
        {
            if(SUCCEEDED(mStatus))
                CoUninitialize();
            mStatus = std::exchange(rhs.mStatus, E_FAIL);
        }
        return *this;
    }
    auto operator=(const ComWrapper&) -> ComWrapper& = delete;

    [[nodiscard]] auto status() const noexcept -> HRESULT { return mStatus; }
    [[nodiscard]] explicit operator bool() const noexcept { return SUCCEEDED(status()); }

    void uninit()
    {
        if(SUCCEEDED(mStatus))
            CoUninitialize();
        mStatus = E_FAIL;
    }
};


template<typename T> /* NOLINTNEXTLINE(clazy-rule-of-three) False positive */
class ComPtr {
    T *mPtr{nullptr};

public:
    using element_type = T;

    static constexpr bool RefIsNoexcept{noexcept(std::declval<T&>().AddRef())
        && noexcept(std::declval<T&>().Release())};

    ComPtr() noexcept = default;
    ComPtr(const ComPtr &rhs) noexcept(RefIsNoexcept) : mPtr{rhs.mPtr}
    { if(mPtr) mPtr->AddRef(); }
    ComPtr(ComPtr&& rhs) noexcept : mPtr{rhs.mPtr} { rhs.mPtr = nullptr; }
    ComPtr(std::nullptr_t) noexcept { } /* NOLINT(google-explicit-constructor) */
    explicit ComPtr(T *const ptr) noexcept : mPtr{ptr} { }
    ~ComPtr() { if(mPtr) mPtr->Release(); }

    /* NOLINTNEXTLINE(bugprone-unhandled-self-assignment) Yes it is. */
    auto operator=(const ComPtr &rhs) noexcept(RefIsNoexcept) -> ComPtr&
    {
        if constexpr(RefIsNoexcept)
        {
            if(rhs.mPtr) rhs.mPtr->AddRef();
            if(mPtr) mPtr->Release();
            mPtr = rhs.mPtr;
            return *this;
        }
        else
        {
            auto tmp = rhs;
            if(mPtr) mPtr->Release();
            mPtr = tmp.release();
            return *this;
        }
    }
    auto operator=(ComPtr&& rhs) noexcept(RefIsNoexcept) -> ComPtr&
    {
        if(&rhs != this) [[likely]]
        {
            if(mPtr) mPtr->Release();
            mPtr = std::exchange(rhs.mPtr, nullptr);
        }
        return *this;
    }

    void reset(T *const ptr=nullptr) noexcept(RefIsNoexcept)
    {
        if(mPtr) mPtr->Release();
        mPtr = ptr;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return mPtr != nullptr; }

    [[nodiscard]] auto operator*() const noexcept -> T& { return *mPtr; }
    [[nodiscard]] auto operator->() const noexcept -> T* { return mPtr; }
    [[nodiscard]] auto get() const noexcept -> T* { return mPtr; }

    [[nodiscard]] auto release() noexcept -> T* { return std::exchange(mPtr, nullptr); }

    void swap(ComPtr &rhs) noexcept { std::swap(mPtr, rhs.mPtr); }
};

template<typename T>
void swap(ComPtr<T> &lhs, ComPtr<T> &rhs) noexcept { lhs.swap(rhs); }
#endif /* _WIN32 */

#endif
