#ifndef EAX_EFFECT_INCLUDED
#define EAX_EFFECT_INCLUDED


#include <memory>

#include "AL/al.h"
#include "core/effects/base.h"
#include "eax_eax_call.h"

class EaxEffect
{
public:
    EaxEffect(ALenum type) : al_effect_type_{type} { }
    virtual ~EaxEffect() = default;

    const ALenum al_effect_type_;
    EffectProps al_effect_props_{};

    // Returns "true" if any immediated property was changed.
    // [[nodiscard]]
    virtual bool dispatch(
        const EaxEaxCall& eax_call) = 0;
}; // EaxEffect


using EaxEffectUPtr = std::unique_ptr<EaxEffect>;


#endif // !EAX_EFFECT_INCLUDED
