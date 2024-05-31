#ifndef EAX_EXCEPTION_INCLUDED
#define EAX_EXCEPTION_INCLUDED

#include <stdexcept>
#include <string>
#include <string_view>


class EaxException : public std::runtime_error {
    static std::string make_message(std::string_view context, std::string_view message);

public:
    EaxException() = delete;
    EaxException(const EaxException&) = delete;
    EaxException(EaxException&&) = delete;
    EaxException(std::string_view context, std::string_view message);
    ~EaxException() override;

    void operator=(const EaxException&) = delete;
    void operator=(EaxException&&) = delete;
};

#endif /* EAX_EXCEPTION_INCLUDED */
