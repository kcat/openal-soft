#include "config.h"

#include "utils.h"

#include <cassert>
#include <exception>

#include "core/logging.h"


void eax_log_exception(std::string_view message) noexcept
{
    const auto exception_ptr = std::current_exception();
    assert(exception_ptr);

    try {
        std::rethrow_exception(exception_ptr);
    }
    catch(const std::exception& ex) {
        ERR("{} {}", message, ex.what());
    }
    catch(...) {
        ERR("{} {}", message, "Generic exception.");
    }
}
