#ifndef CORE_EXCEPT_H
#define CORE_EXCEPT_H

#include <cstdarg>
#include <exception>
#include <string>


namespace al {

class base_exception : public std::exception {
    std::string mMessage;

public:
    base_exception() = default;
    template<typename T, std::enable_if_t<!std::is_same_v<T,base_exception>,bool> = true>
    base_exception(T&& msg) : mMessage{std::forward<T>(msg)} { }
    base_exception(const base_exception&) = default;
    base_exception(base_exception&&) = default;
    ~base_exception() override;

    auto operator=(const base_exception&) -> base_exception& = default;
    auto operator=(base_exception&&) -> base_exception& = default;

    [[nodiscard]] auto what() const noexcept -> const char* override { return mMessage.c_str(); }
};

} // namespace al

#endif /* CORE_EXCEPT_H */
