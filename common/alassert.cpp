
#include "alassert.h"

#include <exception>
#include <stdexcept>
#include <string>

namespace al {

[[noreturn]]
void do_assert(const char *message, int linenum, const char *filename, const char *funcname) noexcept
{
    std::string errstr{filename};
    errstr += ':';
    errstr += std::to_string(linenum);
    errstr += ": ";
    errstr += funcname;
    errstr += ": ";
    errstr += message;
    /* Calling std::terminate in a catch block hopefully causes the system to
     * provide info about the caught exception in the error dialog. At least on
     * Linux, this results in the process printing
     *
     * terminate called after throwing an instance of 'std::runtime_error'
     *   what():  <message here>
     *
     * before terminating from a SIGABRT. Hopefully Windows and Mac will do the
     * appropriate things with the message for an abnormal termination.
     */
    try {
        throw std::runtime_error{errstr};
    }
    catch(...) {
        std::terminate();
    }
}

} /* namespace al */
