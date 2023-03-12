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

constexpr ALenum EnumFromEaxEffectType(const EaxEffectProps &props)
{
    switch(props.mType)
    {
    case EaxEffectType::None: break;
    case EaxEffectType::Reverb: return AL_EFFECT_EAXREVERB;
    case EaxEffectType::Chorus: return AL_EFFECT_CHORUS;
    case EaxEffectType::Autowah: return AL_EFFECT_AUTOWAH;
    case EaxEffectType::Compressor: return AL_EFFECT_COMPRESSOR;
    case EaxEffectType::Distortion: return AL_EFFECT_DISTORTION;
    case EaxEffectType::Echo: return AL_EFFECT_ECHO;
    case EaxEffectType::Equalizer: return AL_EFFECT_EQUALIZER;
    case EaxEffectType::Flanger: return AL_EFFECT_FLANGER;
    case EaxEffectType::FrequencyShifter: return AL_EFFECT_FREQUENCY_SHIFTER;
    case EaxEffectType::Modulator: return AL_EFFECT_RING_MODULATOR;
    case EaxEffectType::PitchShifter: return AL_EFFECT_PITCH_SHIFTER;
    case EaxEffectType::VocalMorpher: return AL_EFFECT_VOCAL_MORPHER;
    }
    return AL_EFFECT_NULL;
}

struct EaxReverbCommitter {
    struct Exception;

    EaxReverbCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : mEaxProps{eaxprops}, mAlProps{alprops}
    { }

    EaxEffectProps &mEaxProps;
    EffectProps &mAlProps;

    [[noreturn]] static void fail(const char* message);
    [[noreturn]] static void fail_unknown_property_id()
    { fail(EaxEffectErrorMessages::unknown_property_id()); }

    template<typename TValidator, typename TProperty>
    static void defer(const EaxCall& call, TProperty& property)
    {
        const auto& value = call.get_value<Exception, const TProperty>();
        TValidator{}(value);
        property = value;
    }

    template<typename TValidator, typename TDeferrer, typename TProperties, typename TProperty>
    static void defer(const EaxCall& call, TProperties& properties, TProperty&)
    {
        const auto& value = call.get_value<Exception, const TProperty>();
        TValidator{}(value);
        TDeferrer{}(properties, value);
    }

    template<typename TValidator, typename TProperty>
    static void defer3(const EaxCall& call, EAXREVERBPROPERTIES& properties, TProperty& property)
    {
        const auto& value = call.get_value<Exception, const TProperty>();
        TValidator{}(value);
        if (value == property)
            return;
        property = value;
        properties.ulEnvironment = EAX_ENVIRONMENT_UNDEFINED;
    }


    bool commit(const EAX_REVERBPROPERTIES &props);
    bool commit(const EAX20LISTENERPROPERTIES &props);
    bool commit(const EAXREVERBPROPERTIES &props);
    bool commit(const EaxEffectProps &props);

    static void SetDefaults(EAX_REVERBPROPERTIES &props);
    static void SetDefaults(EAX20LISTENERPROPERTIES &props);
    static void SetDefaults(EAXREVERBPROPERTIES &props);
    static void SetDefaults(EaxEffectProps &props);

    static void Get(const EaxCall &call, const EAX_REVERBPROPERTIES &props);
    static void Get(const EaxCall &call, const EAX20LISTENERPROPERTIES &props);
    static void Get(const EaxCall &call, const EAXREVERBPROPERTIES &props);
    static void Get(const EaxCall &call, const EaxEffectProps &props);

    static void Set(const EaxCall &call, EAX_REVERBPROPERTIES &props);
    static void Set(const EaxCall &call, EAX20LISTENERPROPERTIES &props);
    static void Set(const EaxCall &call, EAXREVERBPROPERTIES &props);
    static void Set(const EaxCall &call, EaxEffectProps &props);

    static void translate(const EAX_REVERBPROPERTIES& src, EaxEffectProps& dst) noexcept;
    static void translate(const EAX20LISTENERPROPERTIES& src, EaxEffectProps& dst) noexcept;
    static void translate(const EAXREVERBPROPERTIES& src, EaxEffectProps& dst) noexcept;
};

template<typename T>
struct EaxCommitter {
    struct Exception;

    EaxCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : mEaxProps{eaxprops}, mAlProps{alprops}
    { }

    EaxEffectProps &mEaxProps;
    EffectProps &mAlProps;

    template<typename TValidator, typename TProperty>
    static void defer(const EaxCall& call, TProperty& property)
    {
        const auto& value = call.get_value<Exception, const TProperty>();
        TValidator{}(value);
        property = value;
    }

    [[noreturn]] static void fail(const char *message);
    [[noreturn]] static void fail_unknown_property_id()
    { fail(EaxEffectErrorMessages::unknown_property_id()); }

    bool commit(const EaxEffectProps &props);

    static void SetDefaults(EaxEffectProps &props);
    static void Get(const EaxCall &call, const EaxEffectProps &props);
    static void Set(const EaxCall &call, EaxEffectProps &props);
};

struct EaxAutowahCommitter : public EaxCommitter<EaxAutowahCommitter> {
    using EaxCommitter<EaxAutowahCommitter>::EaxCommitter;
};
struct EaxChorusCommitter : public EaxCommitter<EaxChorusCommitter> {
    using EaxCommitter<EaxChorusCommitter>::EaxCommitter;
};
struct EaxCompressorCommitter : public EaxCommitter<EaxCompressorCommitter> {
    using EaxCommitter<EaxCompressorCommitter>::EaxCommitter;
};
struct EaxDistortionCommitter : public EaxCommitter<EaxDistortionCommitter> {
    using EaxCommitter<EaxDistortionCommitter>::EaxCommitter;
};
struct EaxEchoCommitter : public EaxCommitter<EaxEchoCommitter> {
    using EaxCommitter<EaxEchoCommitter>::EaxCommitter;
};
struct EaxEqualizerCommitter : public EaxCommitter<EaxEqualizerCommitter> {
    using EaxCommitter<EaxEqualizerCommitter>::EaxCommitter;
};
struct EaxFlangerCommitter : public EaxCommitter<EaxFlangerCommitter> {
    using EaxCommitter<EaxFlangerCommitter>::EaxCommitter;
};
struct EaxFrequencyShifterCommitter : public EaxCommitter<EaxFrequencyShifterCommitter> {
    using EaxCommitter<EaxFrequencyShifterCommitter>::EaxCommitter;
};
struct EaxModulatorCommitter : public EaxCommitter<EaxModulatorCommitter> {
    using EaxCommitter<EaxModulatorCommitter>::EaxCommitter;
};
struct EaxPitchShifterCommitter : public EaxCommitter<EaxPitchShifterCommitter> {
    using EaxCommitter<EaxPitchShifterCommitter>::EaxCommitter;
};
struct EaxVocalMorpherCommitter : public EaxCommitter<EaxVocalMorpherCommitter> {
    using EaxCommitter<EaxVocalMorpherCommitter>::EaxCommitter;
};
struct EaxNullCommitter : public EaxCommitter<EaxNullCommitter> {
    using EaxCommitter<EaxNullCommitter>::EaxCommitter;
};


class EaxEffect {
public:
    EaxEffect() noexcept = default;
    ~EaxEffect() = default;

    ALenum al_effect_type_{AL_EFFECT_NULL};
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

    int version_{};
    bool changed_{};
    Props4 props_{};
    State1 state1_{};
    State2 state2_{};
    State3 state3_{};
    State4 state4_{};
    State4 state5_{};


    template<typename T, typename ...Args>
    void call_set_defaults(Args&& ...args)
    { return T::SetDefaults(std::forward<Args>(args)...); }

    void call_set_defaults(const ALenum altype, EaxEffectProps &props)
    {
        if(altype == AL_EFFECT_EAXREVERB)
            return call_set_defaults<EaxReverbCommitter>(props);
        if(altype == AL_EFFECT_CHORUS)
            return call_set_defaults<EaxChorusCommitter>(props);
        if(altype == AL_EFFECT_AUTOWAH)
            return call_set_defaults<EaxAutowahCommitter>(props);
        if(altype == AL_EFFECT_COMPRESSOR)
            return call_set_defaults<EaxCompressorCommitter>(props);
        if(altype == AL_EFFECT_DISTORTION)
            return call_set_defaults<EaxDistortionCommitter>(props);
        if(altype == AL_EFFECT_ECHO)
            return call_set_defaults<EaxEchoCommitter>(props);
        if(altype == AL_EFFECT_EQUALIZER)
            return call_set_defaults<EaxEqualizerCommitter>(props);
        if(altype == AL_EFFECT_FLANGER)
            return call_set_defaults<EaxFlangerCommitter>(props);
        if(altype == AL_EFFECT_FREQUENCY_SHIFTER)
            return call_set_defaults<EaxFrequencyShifterCommitter>(props);
        if(altype == AL_EFFECT_RING_MODULATOR)
            return call_set_defaults<EaxModulatorCommitter>(props);
        if(altype == AL_EFFECT_PITCH_SHIFTER)
            return call_set_defaults<EaxPitchShifterCommitter>(props);
        if(altype == AL_EFFECT_VOCAL_MORPHER)
            return call_set_defaults<EaxVocalMorpherCommitter>(props);
        return call_set_defaults<EaxNullCommitter>(props);
    }

    template<typename T>
    void init()
    {
        call_set_defaults<EaxReverbCommitter>(state1_.d);
        state1_.i = state1_.d;
        call_set_defaults<EaxReverbCommitter>(state2_.d);
        state2_.i = state2_.d;
        call_set_defaults<EaxReverbCommitter>(state3_.d);
        state3_.i = state3_.d;
        call_set_defaults<T>(state4_.d);
        state4_.i = state4_.d;
        call_set_defaults<T>(state5_.d);
        state5_.i = state5_.d;
    }

    void set_defaults(int eax_version, ALenum altype)
    {
        switch(eax_version)
        {
        case 1: call_set_defaults<EaxReverbCommitter>(state1_.d); break;
        case 2: call_set_defaults<EaxReverbCommitter>(state2_.d); break;
        case 3: call_set_defaults<EaxReverbCommitter>(state3_.d); break;
        case 4: call_set_defaults(altype, state4_.d); break;
        case 5: call_set_defaults(altype, state5_.d); break;
        }
        changed_ = true;
    }


#define EAXCALL(T, Callable, ...)                                             \
    if(T == EaxEffectType::Reverb)                                            \
        return Callable<EaxReverbCommitter>(__VA_ARGS__);                     \
    if(T == EaxEffectType::Chorus)                                            \
        return Callable<EaxChorusCommitter>(__VA_ARGS__);                     \
    if(T == EaxEffectType::Autowah)                                           \
        return Callable<EaxAutowahCommitter>(__VA_ARGS__);                    \
    if(T == EaxEffectType::Compressor)                                        \
        return Callable<EaxCompressorCommitter>(__VA_ARGS__);                 \
    if(T == EaxEffectType::Distortion)                                        \
        return Callable<EaxDistortionCommitter>(__VA_ARGS__);                 \
    if(T == EaxEffectType::Echo)                                              \
        return Callable<EaxEchoCommitter>(__VA_ARGS__);                       \
    if(T == EaxEffectType::Equalizer)                                         \
        return Callable<EaxEqualizerCommitter>(__VA_ARGS__);                  \
    if(T == EaxEffectType::Flanger)                                           \
        return Callable<EaxFlangerCommitter>(__VA_ARGS__);                    \
    if(T == EaxEffectType::FrequencyShifter)                                  \
        return Callable<EaxFrequencyShifterCommitter>(__VA_ARGS__);           \
    if(T == EaxEffectType::Modulator)                                         \
        return Callable<EaxModulatorCommitter>(__VA_ARGS__);                  \
    if(T == EaxEffectType::PitchShifter)                                      \
        return Callable<EaxPitchShifterCommitter>(__VA_ARGS__);               \
    if(T == EaxEffectType::VocalMorpher)                                      \
        return Callable<EaxVocalMorpherCommitter>(__VA_ARGS__);               \
    return Callable<EaxNullCommitter>(__VA_ARGS__)

    template<typename T, typename ...Args>
    static void call_set(Args&& ...args)
    { return T::Set(std::forward<Args>(args)...); }

    static void call_set(const EaxCall &call, EaxEffectProps &props)
    { EAXCALL(props.mType, call_set, call, props); }

    void set(const EaxCall &call)
    {
        switch(call.get_version())
        {
        case 1: call_set<EaxReverbCommitter>(call, state1_.d); break;
        case 2: call_set<EaxReverbCommitter>(call, state2_.d); break;
        case 3: call_set<EaxReverbCommitter>(call, state3_.d); break;
        case 4: call_set(call, state4_.d); break;
        case 5: call_set(call, state5_.d); break;
        }
        changed_ = true;
    }


    template<typename T, typename ...Args>
    static void call_get(Args&& ...args)
    { return T::Get(std::forward<Args>(args)...); }

    static void call_get(const EaxCall &call, const EaxEffectProps &props)
    { EAXCALL(props.mType, call_get, call, props); }

    void get(const EaxCall &call)
    {
        switch(call.get_version())
        {
        case 1: call_get<EaxReverbCommitter>(call, state1_.d); break;
        case 2: call_get<EaxReverbCommitter>(call, state2_.d); break;
        case 3: call_get<EaxReverbCommitter>(call, state3_.d); break;
        case 4: call_get(call, state4_.d); break;
        case 5: call_get(call, state5_.d); break;
        }
    }


    template<typename T, typename ...Args>
    bool call_commit(Args&& ...args)
    { return T{props_, al_effect_props_}.commit(std::forward<Args>(args)...); }

    bool call_commit(const EaxEffectProps &props)
    { EAXCALL(props.mType, call_commit, props); }

    bool commit(int eax_version)
    {
        changed_ |= version_ != eax_version;
        if(!changed_) return false;

        bool ret{version_ != eax_version};
        version_ = eax_version;
        changed_ = false;

        switch(eax_version)
        {
        case 1:
            state1_.i = state1_.d;
            ret |= call_commit<EaxReverbCommitter>(state1_.d);
            break;
        case 2:
            state2_.i = state2_.d;
            ret |= call_commit<EaxReverbCommitter>(state2_.d);
            break;
        case 3:
            state3_.i = state3_.d;
            ret |= call_commit<EaxReverbCommitter>(state3_.d);
            break;
        case 4:
            state4_.i = state4_.d;
            ret |= call_commit(state4_.d);
            break;
        case 5:
            state5_.i = state5_.d;
            ret |= call_commit(state5_.d);
            break;
        }
        al_effect_type_ = EnumFromEaxEffectType(props_);
        return ret;
    }
#undef EAXCALL
}; // EaxEffect

using EaxEffectUPtr = std::unique_ptr<EaxEffect>;

#endif // !EAX_EFFECT_INCLUDED
