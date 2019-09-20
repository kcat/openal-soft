#ifndef ALEXCPT_H
#define ALEXCPT_H

#include <exception>
#include <string>

#include "AL/alc.h"


#ifdef __GNUC__
#define ALEXCPT_FORMAT(x, y, z) __attribute__((format(x, (y), (z))))
#else
#define ALEXCPT_FORMAT(x, y, z)
#endif


namespace al {

class backend_exception final : public std::exception {
    std::string mMessage;
    ALCenum mErrorCode;

public:
    backend_exception(ALCenum code, const char *msg, ...) ALEXCPT_FORMAT(printf, 3,4);

    const char *what() const noexcept override { return mMessage.c_str(); }
    ALCenum errorCode() const noexcept { return mErrorCode; }
};

} // namespace al

#define START_API_FUNC try

#ifndef _MSC_VER
#define END_API_FUNC catch(...) { std::terminate(); }
#else
/* VS 2015 complains that some of these catch statements are unreachable code,
 * due to the function body not able to throw anything. While technically true,
 * it's preferable to mark API functions just in case that ever changes, so
 * silence that warning.
 */
#define END_API_FUNC __pragma(warning(push)) \
__pragma(warning(disable : 4702))            \
catch(...) { std::terminate(); }             \
__pragma(warning(pop))
#endif

#endif /* ALEXCPT_H */
