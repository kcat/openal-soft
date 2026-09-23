
#include "config.h"

#include <cstring>
#include <mutex>
#if defined(_WIN32)
#include <guiddef.h>
#endif

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "direct_defs.h"
#include "eax/alapi.hpp"
#include "eax/api.h"
#include "eax/exception.h"

#if HAVE_CXXMODULES
import alc.context;
import gsl;
import logging;
#else
#include "alc/context.hpp"
#include "core/logging.h"
#include "gsl/gsl"
#endif


namespace {

void eax_log_exception(std::string_view const message) noexcept
{
    if(auto const exception_ptr = std::current_exception(); !exception_ptr) [[unlikely]]
        ERR("{} {}", message, "No exception.");
    else try {
        std::rethrow_exception(exception_ptr);
    }
    catch(std::exception& ex) {
        ERR("{} {}", message, ex.what());
    }
    catch(...) {
        ERR("{} {}", message, "Generic exception.");
    }
}

[[nodiscard]]
auto get_alguid(_GUID const *const guid) -> AL_GUID
{
#if defined(_WIN32)
    static_assert(sizeof(_GUID) == sizeof(AL_GUID));
#endif
    if(!guid)
        throw EaxException{"EAX_CALL", "Null property set ID."};
    auto store = AL_GUID{};
    std::memcpy(&store, guid, sizeof(AL_GUID));
    return store;
}

auto EAXSet_(gsl::not_null<al::Context*> context, _GUID const *property_set_id,
    ALuint property_id, ALuint source_id, ALvoid *value, ALuint value_size) noexcept -> ALenum
try {
    auto const proplock = std::lock_guard{context->mPropLock};
    return context->eax_eax_set(get_alguid(property_set_id), property_id, source_id, value,
        value_size);
}
catch(...) {
    context->eaxSetLastError();
    eax_log_exception(std::data(__func__));
    return AL_INVALID_OPERATION;
}

auto EAXGet_(gsl::not_null<al::Context*> context, _GUID const *property_set_id,
    ALuint property_id, ALuint source_id, ALvoid *value, ALuint value_size) noexcept -> ALenum
try {
    auto const proplock = std::lock_guard{context->mPropLock};
    return context->eax_eax_get(get_alguid(property_set_id), property_id, source_id, value,
        value_size);
}
catch(...) {
    context->eaxSetLastError();
    eax_log_exception(std::data(__func__));
    return AL_INVALID_OPERATION;
}

} // namespace

DECL_FUNC(FORCE_ALIGN, ALenum, EAXSet, _GUID const*,property_set_id, ALuint,property_id,
    ALuint,source_id, ALvoid*,value, ALuint,value_size)
DECL_FUNC(FORCE_ALIGN, ALenum, EAXGet, _GUID const*,property_set_id, ALuint,property_id,
    ALuint,source_id, ALvoid*,value, ALuint,value_size)
