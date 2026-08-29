
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
#include "eax/api.h"
#include "eax/exception.h"
#include "eax/utils.h"

#if HAVE_CXXMODULES
import alc.context;
import gsl;
#else
#include "alc/context.hpp"
#include "gsl/gsl"
#endif


namespace {

#if defined(_WIN32)
static_assert(sizeof(_GUID) == sizeof(AL_GUID));
#endif

auto get_alguid(_GUID const *const guid, AL_GUID *store) -> AL_GUID&
{
    if(!guid)
        throw EaxException{"EAX_CALL", "Null property set ID."};
    std::memcpy(store, guid, sizeof(AL_GUID));
    return *store;
}

auto EAXSet_(gsl::not_null<al::Context*> context, _GUID const *property_set_id,
    ALuint property_id, ALuint source_id, ALvoid *value, ALuint value_size) noexcept -> ALenum
try {
    const auto proplock = std::lock_guard{context->mPropLock};
    auto guid = AL_GUID{};
    return context->eax_eax_set(get_alguid(property_set_id, &guid), property_id, source_id, value,
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
    const auto proplock = std::lock_guard{context->mPropLock};
    auto guid = AL_GUID{};
    return context->eax_eax_get(get_alguid(property_set_id, &guid), property_id, source_id, value,
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
