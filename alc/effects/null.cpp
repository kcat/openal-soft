
#include "config.h"

#include "AL/al.h"
#include "AL/alc.h"

#include "al/auxeffectslot.h"
#include "alcmain.h"
#include "alcontext.h"
#include "almalloc.h"
#include "alspan.h"
#include "effects/base.h"


namespace {

struct NullState final : public EffectState {
    NullState();
    ~NullState() override;

    void deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const EffectSlot *slot, const EffectProps *props,
        const EffectTarget target) override;
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn,
        const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(NullState)
};

/* This constructs the effect state. It's called when the object is first
 * created.
 */
NullState::NullState() = default;

/* This destructs the effect state. It's called only when the effect instance
 * is no longer used.
 */
NullState::~NullState() = default;

/* This updates the device-dependant effect state. This is called on state
 * initialization and any time the device parameters (e.g. playback frequency,
 * format) have been changed. Will always be followed by a call to the update
 * method, if successful.
 */
void NullState::deviceUpdate(const ALCdevice* /*device*/)
{
}

/* This updates the effect state with new properties. This is called any time
 * the effect is (re)loaded into a slot.
 */
void NullState::update(const ALCcontext* /*context*/, const EffectSlot* /*slot*/,
    const EffectProps* /*props*/, const EffectTarget /*target*/)
{
}

/* This processes the effect state, for the given number of samples from the
 * input to the output buffer. The result should be added to the output buffer,
 * not replace it.
 */
void NullState::process(const size_t/*samplesToDo*/,
    const al::span<const FloatBufferLine> /*samplesIn*/,
    const al::span<FloatBufferLine> /*samplesOut*/)
{
}


struct NullStateFactory final : public EffectStateFactory {
    EffectState *create() override;
};

/* Creates EffectState objects of the appropriate type. */
EffectState *NullStateFactory::create()
{ return new NullState{}; }

} // namespace

EffectStateFactory *NullStateFactory_getFactory()
{
    static NullStateFactory NullFactory{};
    return &NullFactory;
}
