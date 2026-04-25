
#include "config.h"

#include <mutex>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "direct_defs.h"
#include "eax/api.h"
#include "eax/utils.h"

#if HAVE_CXXMODULES
import alc.context;
import gsl;
#else
#include "alc/context.hpp"
#include "gsl/gsl"
#endif


namespace {

auto EAXSet_(gsl::not_null<al::Context*> context, _GUID const *property_set_id,
    ALuint property_id, ALuint source_id, ALvoid *value, ALuint value_size) noexcept -> ALenum
try {
    const auto proplock = std::lock_guard{context->mPropLock};
    return context->eax_eax_set(std::launder(reinterpret_cast<AL_GUID const*>(property_set_id)),
        property_id, source_id, value, value_size);
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
    return context->eax_eax_get(std::launder(reinterpret_cast<AL_GUID const*>(property_set_id)),
        property_id, source_id, value, value_size);
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
