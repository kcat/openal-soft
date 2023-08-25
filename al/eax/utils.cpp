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
        const auto ex_message = ex.what();
        ERR("%.*s %s\n", static_cast<int>(message.length()), message.data(), ex_message);
    }
    catch(...) {
        ERR("%.*s %s\n", static_cast<int>(message.length()), message.data(), "Generic exception.");
    }
}
