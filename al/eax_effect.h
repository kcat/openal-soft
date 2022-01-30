#ifndef EAX_EFFECT_INCLUDED
#define EAX_EFFECT_INCLUDED


#include <memory>

#include "eax_eax_call.h"


class EaxEffect
{
public:
    EaxEffect() = default;

    virtual ~EaxEffect() = default;


    // Returns "true" if any immediated property was changed.
    // [[nodiscard]]
    virtual bool dispatch(
        const EaxEaxCall& eax_call) = 0;
}; // EaxEffect


using EaxEffectUPtr = std::unique_ptr<EaxEffect>;


#endif // !EAX_EFFECT_INCLUDED
