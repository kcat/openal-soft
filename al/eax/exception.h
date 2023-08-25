#ifndef EAX_EXCEPTION_INCLUDED
#define EAX_EXCEPTION_INCLUDED

#include <stdexcept>
#include <string>
#include <string_view>


class EaxException : public std::runtime_error {
    static std::string make_message(std::string_view context, std::string_view message);

public:
    EaxException(std::string_view context, std::string_view message);
    ~EaxException() override;
}; // EaxException


#endif // !EAX_EXCEPTION_INCLUDED
