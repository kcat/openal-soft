#ifndef CORE_EXCEPT_H
#define CORE_EXCEPT_H

#include <cstdarg>
#include <exception>
#include <string>
#include <utility>


namespace al {

class base_exception : public std::exception {
    std::string mMessage;

protected:
    auto setMessage(const char *msg, std::va_list args) -> void;

public:
    base_exception() = default;
    ~base_exception() override;

    [[nodiscard]] auto what() const noexcept -> const char* override { return mMessage.c_str(); }
};

} // namespace al

#define START_API_FUNC try

#define END_API_FUNC catch(...) { std::terminate(); }

#endif /* CORE_EXCEPT_H */
