#ifndef ALEXCPT_H
#define ALEXCPT_H

#include <exception>
#include <string>

#include "AL/alc.h"


namespace al {

class backend_exception final : public std::exception {
    std::string mMessage;
    ALCenum mErrorCode;

public:
    backend_exception(ALCenum code, const char *msg, ...);

    const char *what() const noexcept override { return mMessage.c_str(); }
    ALCenum errorCode() const noexcept { return mErrorCode; }
};

} // namespace al

#define START_API_FUNC try

#define END_API_FUNC catch(...) { std::terminate(); }

#endif /* ALEXCPT_H */
