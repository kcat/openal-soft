module;

#include "core/except.h"
#include "opthelpers.h"

export module backends.exception;

import format;

export namespace al {

    enum class backend_error {
        NoDevice,
        DeviceError,
        OutOfMemory
    };

    /* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
    extern "C++" class backend_exception final : public base_exception {
        backend_error mErrorCode;

        static auto make_string(al::string_view fmt, al::format_args args) -> std::string;

    public:
        template<typename ...Args>
        backend_exception(backend_error const code, al::format_string<Args...> fmt, Args&& ...args)
            : base_exception{
                assign_result{[&]{ return make_string(fmt.get(), al::make_format_args(args...)); }}
            }, mErrorCode{code}
        { }
        backend_exception(const backend_exception&) = default;
        backend_exception(backend_exception&&) = default;
        NOINLINE ~backend_exception() override = default;

        auto operator=(const backend_exception&) LIFETIMEBOUND -> backend_exception& = default;
        auto operator=(backend_exception&&) LIFETIMEBOUND -> backend_exception& = default;

        [[nodiscard]] auto errorCode() const noexcept -> backend_error { return mErrorCode; }
    };

}
