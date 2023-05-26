#ifndef EAX_EXCEPTION_INCLUDED
#define EAX_EXCEPTION_INCLUDED


#include <stdexcept>
#include <string>


class EaxException : public std::runtime_error {
    static std::string make_message(const char *context, const char *message);

public:
    EaxException(const char *context, const char *message);
    ~EaxException() override;
}; // EaxException


#endif // !EAX_EXCEPTION_INCLUDED
