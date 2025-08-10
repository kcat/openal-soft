
#include "config.h"

#include "AL/al.h"

#include "alc/context.h"
#include "direct_defs.h"
#include "eax/utils.h"
#include "gsl/gsl"


namespace {

auto EAXSet(gsl::not_null<al::Context*> context, const GUID *property_set_id,
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

auto EAXGet(gsl::not_null<al::Context*> context, const GUID *property_set_id,
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

FORCE_ALIGN DECL_FUNC5(ALenum, EAXSet, const GUID*,property_set_id, ALuint,property_id,
    ALuint,source_id, ALvoid*,value, ALuint,value_size)
FORCE_ALIGN DECL_FUNC5(ALenum, EAXGet, const GUID*,property_set_id, ALuint,property_id,
    ALuint,source_id, ALvoid*,value, ALuint,value_size)
