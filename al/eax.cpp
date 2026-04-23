
#include "config.h"

#include <mutex>

#include "AL/al.h"
#include "AL/alc.h"

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

auto EAXSet_(gsl::not_null<al::Context*> context, const GUID *property_set_id,
    ALuint property_id, ALuint source_id, ALvoid *value, ALuint value_size) noexcept -> ALenum
try {
    const auto proplock = std::lock_guard{context->mPropLock};
    return context->eax_eax_set(property_set_id, property_id, source_id, value, value_size);
}
catch(...) {
    context->eaxSetLastError();
    eax_log_exception(std::data(__func__));
    return AL_INVALID_OPERATION;
}

auto EAXGet_(gsl::not_null<al::Context*> context, const GUID *property_set_id,
    ALuint property_id, ALuint source_id, ALvoid *value, ALuint value_size) noexcept -> ALenum
try {
    const auto proplock = std::lock_guard{context->mPropLock};
    return context->eax_eax_get(property_set_id, property_id, source_id, value, value_size);
}
catch(...) {
    context->eaxSetLastError();
    eax_log_exception(std::data(__func__));
    return AL_INVALID_OPERATION;
}

} // namespace

DECL_FUNC(FORCE_ALIGN, ALenum, EAXSet, const GUID*,property_set_id, ALuint,property_id,
    ALuint,source_id, ALvoid*,value, ALuint,value_size)
DECL_FUNC(FORCE_ALIGN, ALenum, EAXGet, const GUID*,property_set_id, ALuint,property_id,
    ALuint,source_id, ALvoid*,value, ALuint,value_size)
