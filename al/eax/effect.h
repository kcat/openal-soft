#ifndef EAX_EFFECT_INCLUDED
#define EAX_EFFECT_INCLUDED


#include <cassert>
#include <memory>

#include "alnumeric.h"
#include "AL/al.h"
#include "core/effects/base.h"
#include "call.h"

struct EaxEffectErrorMessages
{
    static constexpr auto unknown_property_id() noexcept { return "Unknown property id."; }
    static constexpr auto unknown_version() noexcept { return "Unknown version."; }
}; // EaxEffectErrorMessages

/* TODO: Use std::variant (C++17). */
enum class EaxEffectType {
    None, Reverb, Chorus, Autowah, Compressor, Distortion, Echo, Equalizer, Flanger,
    FrequencyShifter, Modulator, PitchShifter, VocalMorpher
};
struct EaxEffectProps {
    EaxEffectType mType;
    union {
        EAXREVERBPROPERTIES mReverb;
        EAXCHORUSPROPERTIES mChorus;
        EAXAUTOWAHPROPERTIES mAutowah;
        EAXAGCCOMPRESSORPROPERTIES mCompressor;
        EAXDISTORTIONPROPERTIES mDistortion;
        EAXECHOPROPERTIES mEcho;
        EAXEQUALIZERPROPERTIES mEqualizer;
        EAXFLANGERPROPERTIES mFlanger;
        EAXFREQUENCYSHIFTERPROPERTIES mFrequencyShifter;
        EAXRINGMODULATORPROPERTIES mModulator;
        EAXPITCHSHIFTERPROPERTIES mPitchShifter;
        EAXVOCALMORPHERPROPERTIES mVocalMorpher;
    };
};

class EaxEffect {
public:
    EaxEffect(ALenum type, int eax_version) noexcept
        : al_effect_type_{type}, version_{eax_version}
    { }
    virtual ~EaxEffect() = default;

    const ALenum al_effect_type_;
    EffectProps al_effect_props_{};

    using Props1 = EAX_REVERBPROPERTIES;
    using Props2 = EAX20LISTENERPROPERTIES;
    using Props3 = EAXREVERBPROPERTIES;
    using Props4 = EaxEffectProps;

    struct State1 {
        Props1 i; // Immediate.
        Props1 d; // Deferred.
    };

    struct State2 {
        Props2 i; // Immediate.
        Props2 d; // Deferred.
    };

    struct State3 {
        Props3 i; // Immediate.
        Props3 d; // Deferred.
    };

    struct State4 {
        Props4 i; // Immediate.
        Props4 d; // Deferred.
    };

    int version_;
    bool changed_{};
    Props4 props_{};
    State1 state1_{};
    State2 state2_{};
    State3 state3_{};
    State4 state4_{};
    State4 state5_{};

    virtual void dispatch(const EaxCall& call) = 0;

    // Returns "true" if any immediated property was changed.
    /*[[nodiscard]]*/ virtual bool commit() = 0;
}; // EaxEffect

// Base class for EAX4+ effects.
template<typename TException>
class EaxEffect4 : public EaxEffect
{
public:
    EaxEffect4(ALenum type, int eax_version)
        : EaxEffect{type, clamp(eax_version, 4, 5)}
    { }

    void initialize()
    {
        set_defaults();
        set_efx_defaults();
    }

    void dispatch(const EaxCall& call) override
    { call.is_get() ? get(call) : set(call); }

    bool commit() final
    {
        switch (version_)
        {
            case 4: return commit_state(state4_);
            case 5: return commit_state(state5_);
            default: fail_unknown_version();
        }
    }

protected:
    using Exception = TException;

    template<typename TValidator, typename TProperty>
    static void defer(const EaxCall& call, TProperty& property)
    {
        const auto& value = call.get_value<Exception, const TProperty>();
        TValidator{}(value);
        property = value;
    }

    virtual void set_defaults(Props4& props) = 0;
    virtual void set_efx_defaults() = 0;

    virtual void get(const EaxCall& call, const Props4& props) = 0;
    virtual void set(const EaxCall& call, Props4& props) = 0;

    virtual bool commit_props(const Props4& props) = 0;

    [[noreturn]] static void fail(const char* message)
    {
        throw Exception{message};
    }

    [[noreturn]] static void fail_unknown_property_id()
    {
        fail(EaxEffectErrorMessages::unknown_property_id());
    }

    [[noreturn]] static void fail_unknown_version()
    {
        fail(EaxEffectErrorMessages::unknown_version());
    }

private:
    void set_defaults()
    {
        set_defaults(props_);
        state4_.i = props_;
        state4_.d = props_;
        state5_.i = props_;
        state5_.d = props_;
    }

    void get(const EaxCall& call)
    {
        switch (call.get_version())
        {
            case 4: get(call, state4_.i); break;
            case 5: get(call, state5_.i); break;
            default: fail_unknown_version();
        }
    }

    void set(const EaxCall& call)
    {
        const auto version = call.get_version();
        switch (version)
        {
            case 4: set(call, state4_.d); break;
            case 5: set(call, state5_.d); break;
            default: fail_unknown_version();
        }
        version_ = version;
    }

    bool commit_state(State4& state)
    {
        const auto props = props_;
        state.i = state.d;
        props_ = state.d;
        return commit_props(props);
    }
}; // EaxEffect4

using EaxEffectUPtr = std::unique_ptr<EaxEffect>;

// Creates EAX4+ effect.
template<typename TEffect>
EaxEffectUPtr eax_create_eax4_effect(int eax_version)
{
    auto effect = std::make_unique<TEffect>(eax_version);
    effect->initialize();
    return effect;
}

EaxEffectUPtr eax_create_eax_null_effect(int eax_version);
EaxEffectUPtr eax_create_eax_chorus_effect(int eax_version);
EaxEffectUPtr eax_create_eax_distortion_effect(int eax_version);
EaxEffectUPtr eax_create_eax_echo_effect(int eax_version);
EaxEffectUPtr eax_create_eax_flanger_effect(int eax_version);
EaxEffectUPtr eax_create_eax_frequency_shifter_effect(int eax_version);
EaxEffectUPtr eax_create_eax_vocal_morpher_effect(int eax_version);
EaxEffectUPtr eax_create_eax_pitch_shifter_effect(int eax_version);
EaxEffectUPtr eax_create_eax_ring_modulator_effect(int eax_version);
EaxEffectUPtr eax_create_eax_auto_wah_effect(int eax_version);
EaxEffectUPtr eax_create_eax_compressor_effect(int eax_version);
EaxEffectUPtr eax_create_eax_equalizer_effect(int eax_version);
EaxEffectUPtr eax_create_eax_reverb_effect(int eax_version);

#endif // !EAX_EFFECT_INCLUDED
