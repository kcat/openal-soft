#include "config.h"

#include "utils.h"

#include <exception>

#include "core/logging.h"
#include "gsl/gsl"


void eax_log_exception(std::string_view message) noexcept
{
    const auto exception_ptr = std::current_exception();
    Expects(exception_ptr);

    try {
        std::rethrow_exception(exception_ptr);
    }
    catch(std::exception& ex) {
        ERR("{} {}", message, ex.what());
    }
    catch(...) {
        ERR("{} {}", message, "Generic exception.");
    }
}
