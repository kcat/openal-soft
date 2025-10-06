#ifndef EAX_EFFECT_INCLUDED
#define EAX_EFFECT_INCLUDED

#include <memory>
#include <string_view>
#include <variant>

#include "AL/al.h"
#include "AL/alext.h"
#include "core/effects/base.h"
#include "call.h"

inline bool EaxTraceCommits{false};

struct EaxEffectErrorMessages {
    static constexpr auto unknown_property_id() noexcept { return "Unknown property id."; }
    static constexpr auto unknown_version() noexcept { return "Unknown version."; }
}; // EaxEffectErrorMessages

using EaxEffectProps = std::variant<std::monostate,
    EAXREVERBPROPERTIES,
    EAXCHORUSPROPERTIES,
    EAXAUTOWAHPROPERTIES,
    EAXAGCCOMPRESSORPROPERTIES,
    EAXDISTORTIONPROPERTIES,
    EAXECHOPROPERTIES,
    EAXEQUALIZERPROPERTIES,
    EAXFLANGERPROPERTIES,
    EAXFREQUENCYSHIFTERPROPERTIES,
    EAXRINGMODULATORPROPERTIES,
    EAXPITCHSHIFTERPROPERTIES,
    EAXVOCALMORPHERPROPERTIES>;

template<typename... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

template<typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

constexpr ALenum EnumFromEaxEffectType(const EaxEffectProps &props)
{
    return std::visit(overloaded{
        [](const std::monostate&) noexcept { return AL_EFFECT_NULL; },
        [](const EAXREVERBPROPERTIES&) noexcept { return AL_EFFECT_EAXREVERB; },
        [](const EAXCHORUSPROPERTIES&) noexcept { return AL_EFFECT_CHORUS; },
        [](const EAXAUTOWAHPROPERTIES&) noexcept { return AL_EFFECT_AUTOWAH; },
        [](const EAXAGCCOMPRESSORPROPERTIES&) noexcept { return AL_EFFECT_COMPRESSOR; },
        [](const EAXDISTORTIONPROPERTIES&) noexcept { return AL_EFFECT_DISTORTION; },
        [](const EAXECHOPROPERTIES&) noexcept { return AL_EFFECT_ECHO; },
        [](const EAXEQUALIZERPROPERTIES&) noexcept { return AL_EFFECT_EQUALIZER; },
        [](const EAXFLANGERPROPERTIES&) noexcept { return AL_EFFECT_FLANGER; },
        [](const EAXFREQUENCYSHIFTERPROPERTIES&) noexcept { return AL_EFFECT_FREQUENCY_SHIFTER; },
        [](const EAXRINGMODULATORPROPERTIES&) noexcept { return AL_EFFECT_RING_MODULATOR; },
        [](const EAXPITCHSHIFTERPROPERTIES&) noexcept { return AL_EFFECT_PITCH_SHIFTER; },
        [](const EAXVOCALMORPHERPROPERTIES&) noexcept { return AL_EFFECT_VOCAL_MORPHER; }
    }, props);
}

struct EaxReverbCommitter {
    struct Exception;

    EaxReverbCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : mEaxProps{eaxprops}, mAlProps{alprops}
    { }

    EaxEffectProps &mEaxProps;
    EffectProps &mAlProps;

    [[noreturn]] static void fail(const std::string_view message);
    [[noreturn]] static void fail_unknown_property_id()
    { fail(EaxEffectErrorMessages::unknown_property_id()); }

    template<typename TValidator, typename TProperty>
    static void defer(const EaxCall& call, TProperty& property)
    {
        const auto &value = call.load<const TProperty>();
        TValidator{}(value);
        property = value;
    }

    template<typename TValidator, typename TDeferrer, typename TProperties, typename TProperty>
    static void defer(const EaxCall& call, TProperties& properties, TProperty&)
    {
        const auto &value = call.load<const TProperty>();
        TValidator{}(value);
        TDeferrer{}(properties, value);
    }

    template<typename TValidator, typename TProperty>
    static void defer3(const EaxCall& call, EAXREVERBPROPERTIES& properties, TProperty& property)
    {
        const auto& value = call.load<const TProperty>();
        TValidator{}(value);
        if(value == property)
            return;
        property = value;
        properties.ulEnvironment = EAX_ENVIRONMENT_UNDEFINED;
    }


    [[nodiscard]] auto commit(const EAX_REVERBPROPERTIES &props) const -> bool;
    [[nodiscard]] auto commit(const EAX20LISTENERPROPERTIES &props) const -> bool;
    [[nodiscard]] auto commit(const EAXREVERBPROPERTIES &props) const -> bool;

    static void SetDefaults(EAX_REVERBPROPERTIES &props);
    static void SetDefaults(EAX20LISTENERPROPERTIES &props);
    static void SetDefaults(EAXREVERBPROPERTIES &props);
    static void SetDefaults(EaxEffectProps &props);

    static void Get(const EaxCall &call, const EAX_REVERBPROPERTIES &props);
    static void Get(const EaxCall &call, const EAX20LISTENERPROPERTIES &props);
    static void Get(const EaxCall &call, const EAXREVERBPROPERTIES &props);

    static void Set(const EaxCall &call, EAX_REVERBPROPERTIES &props);
    static void Set(const EaxCall &call, EAX20LISTENERPROPERTIES &props);
    static void Set(const EaxCall &call, EAXREVERBPROPERTIES &props);

    static void translate(const EAX_REVERBPROPERTIES& src, EAXREVERBPROPERTIES& dst) noexcept;
    static void translate(const EAX20LISTENERPROPERTIES& src, EAXREVERBPROPERTIES& dst) noexcept;
};

template<typename T>
struct EaxCommitter {
    struct Exception;

    EaxEffectProps &mEaxProps;
    EffectProps &mAlProps;

    template<typename TValidator, typename TProperty>
    static void defer(const EaxCall &call, TProperty &property)
    {
        const auto &value = call.load<const TProperty>();
        TValidator{}(value);
        property = value;
    }

    [[noreturn]] static void fail(const std::string_view message);
    [[noreturn]] static void fail_unknown_property_id()
    { fail(EaxEffectErrorMessages::unknown_property_id()); }

private:
    EaxCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : mEaxProps{eaxprops}, mAlProps{alprops}
    { }

    friend T;
};

struct EaxAutowahCommitter : public EaxCommitter<EaxAutowahCommitter> {
    EaxAutowahCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : EaxCommitter{eaxprops, alprops}
    { }

    [[nodiscard]] auto commit(const EAXAUTOWAHPROPERTIES &props) const -> bool;

    static void SetDefaults(EaxEffectProps &props);
    static void Get(const EaxCall &call, const EAXAUTOWAHPROPERTIES &props);
    static void Set(const EaxCall &call, EAXAUTOWAHPROPERTIES &props);
};
struct EaxChorusCommitter : public EaxCommitter<EaxChorusCommitter> {
    EaxChorusCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : EaxCommitter{eaxprops, alprops}
    { }

    [[nodiscard]] auto commit(const EAXCHORUSPROPERTIES &props) const -> bool;

    static void SetDefaults(EaxEffectProps &props);
    static void Get(const EaxCall &call, const EAXCHORUSPROPERTIES &props);
    static void Set(const EaxCall &call, EAXCHORUSPROPERTIES &props);
};
struct EaxCompressorCommitter : public EaxCommitter<EaxCompressorCommitter> {
    EaxCompressorCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : EaxCommitter{eaxprops, alprops}
    { }

    [[nodiscard]] auto commit(const EAXAGCCOMPRESSORPROPERTIES &props) const -> bool;

    static void SetDefaults(EaxEffectProps &props);
    static void Get(const EaxCall &call, const EAXAGCCOMPRESSORPROPERTIES &props);
    static void Set(const EaxCall &call, EAXAGCCOMPRESSORPROPERTIES &props);
};
struct EaxDistortionCommitter : public EaxCommitter<EaxDistortionCommitter> {
    EaxDistortionCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : EaxCommitter{eaxprops, alprops}
    { }

    [[nodiscard]] auto commit(const EAXDISTORTIONPROPERTIES &props) const -> bool;

    static void SetDefaults(EaxEffectProps &props);
    static void Get(const EaxCall &call, const EAXDISTORTIONPROPERTIES &props);
    static void Set(const EaxCall &call, EAXDISTORTIONPROPERTIES &props);
};
struct EaxEchoCommitter : public EaxCommitter<EaxEchoCommitter> {
    EaxEchoCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : EaxCommitter{eaxprops, alprops}
    { }

    [[nodiscard]] auto commit(const EAXECHOPROPERTIES &props) const -> bool;

    static void SetDefaults(EaxEffectProps &props);
    static void Get(const EaxCall &call, const EAXECHOPROPERTIES &props);
    static void Set(const EaxCall &call, EAXECHOPROPERTIES &props);
};
struct EaxEqualizerCommitter : public EaxCommitter<EaxEqualizerCommitter> {
    EaxEqualizerCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : EaxCommitter{eaxprops, alprops}
    { }

    [[nodiscard]] auto commit(const EAXEQUALIZERPROPERTIES &props) const -> bool;

    static void SetDefaults(EaxEffectProps &props);
    static void Get(const EaxCall &call, const EAXEQUALIZERPROPERTIES &props);
    static void Set(const EaxCall &call, EAXEQUALIZERPROPERTIES &props);
};
struct EaxFlangerCommitter : public EaxCommitter<EaxFlangerCommitter> {
    EaxFlangerCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : EaxCommitter{eaxprops, alprops}
    { }

    [[nodiscard]] auto commit(const EAXFLANGERPROPERTIES &props) const -> bool;

    static void SetDefaults(EaxEffectProps &props);
    static void Get(const EaxCall &call, const EAXFLANGERPROPERTIES &props);
    static void Set(const EaxCall &call, EAXFLANGERPROPERTIES &props);
};
struct EaxFrequencyShifterCommitter : public EaxCommitter<EaxFrequencyShifterCommitter> {
    EaxFrequencyShifterCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : EaxCommitter{eaxprops, alprops}
    { }

    [[nodiscard]] auto commit(const EAXFREQUENCYSHIFTERPROPERTIES &props) const -> bool;

    static void SetDefaults(EaxEffectProps &props);
    static void Get(const EaxCall &call, const EAXFREQUENCYSHIFTERPROPERTIES &props);
    static void Set(const EaxCall &call, EAXFREQUENCYSHIFTERPROPERTIES &props);
};
struct EaxModulatorCommitter : public EaxCommitter<EaxModulatorCommitter> {
    EaxModulatorCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : EaxCommitter{eaxprops, alprops}
    { }

    [[nodiscard]] auto commit(const EAXRINGMODULATORPROPERTIES &props) const -> bool;

    static void SetDefaults(EaxEffectProps &props);
    static void Get(const EaxCall &call, const EAXRINGMODULATORPROPERTIES &props);
    static void Set(const EaxCall &call, EAXRINGMODULATORPROPERTIES &props);
};
struct EaxPitchShifterCommitter : public EaxCommitter<EaxPitchShifterCommitter> {
    EaxPitchShifterCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : EaxCommitter{eaxprops, alprops}
    { }

    [[nodiscard]] auto commit(const EAXPITCHSHIFTERPROPERTIES &props) const -> bool;

    static void SetDefaults(EaxEffectProps &props);
    static void Get(const EaxCall &call, const EAXPITCHSHIFTERPROPERTIES &props);
    static void Set(const EaxCall &call, EAXPITCHSHIFTERPROPERTIES &props);
};
struct EaxVocalMorpherCommitter : public EaxCommitter<EaxVocalMorpherCommitter> {
    EaxVocalMorpherCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : EaxCommitter{eaxprops, alprops}
    { }

    [[nodiscard]] auto commit(const EAXVOCALMORPHERPROPERTIES &props) const -> bool;

    static void SetDefaults(EaxEffectProps &props);
    static void Get(const EaxCall &call, const EAXVOCALMORPHERPROPERTIES &props);
    static void Set(const EaxCall &call, EAXVOCALMORPHERPROPERTIES &props);
};
struct EaxNullCommitter : public EaxCommitter<EaxNullCommitter> {
    EaxNullCommitter(EaxEffectProps &eaxprops, EffectProps &alprops)
        : EaxCommitter{eaxprops, alprops}
    { }

    [[nodiscard]] auto commit(const std::monostate &props) const -> bool;

    static void SetDefaults(EaxEffectProps &props);
    static void Get(const EaxCall &call, const std::monostate &props);
    static void Set(const EaxCall &call, std::monostate &props);
};

template<typename T>
struct CommitterFromProps { };

template<> struct CommitterFromProps<std::monostate> { using type = EaxNullCommitter; };
template<> struct CommitterFromProps<EAXREVERBPROPERTIES> { using type = EaxReverbCommitter; };
template<> struct CommitterFromProps<EAXCHORUSPROPERTIES> { using type = EaxChorusCommitter; };
template<> struct CommitterFromProps<EAXAGCCOMPRESSORPROPERTIES> { using type = EaxCompressorCommitter; };
template<> struct CommitterFromProps<EAXAUTOWAHPROPERTIES> { using type = EaxAutowahCommitter; };
template<> struct CommitterFromProps<EAXDISTORTIONPROPERTIES> { using type = EaxDistortionCommitter; };
template<> struct CommitterFromProps<EAXECHOPROPERTIES> { using type = EaxEchoCommitter; };
template<> struct CommitterFromProps<EAXEQUALIZERPROPERTIES> { using type = EaxEqualizerCommitter; };
template<> struct CommitterFromProps<EAXFLANGERPROPERTIES> { using type = EaxFlangerCommitter; };
template<> struct CommitterFromProps<EAXFREQUENCYSHIFTERPROPERTIES> { using type = EaxFrequencyShifterCommitter; };
template<> struct CommitterFromProps<EAXRINGMODULATORPROPERTIES> { using type = EaxModulatorCommitter; };
template<> struct CommitterFromProps<EAXPITCHSHIFTERPROPERTIES> { using type = EaxPitchShifterCommitter; };
template<> struct CommitterFromProps<EAXVOCALMORPHERPROPERTIES> { using type = EaxVocalMorpherCommitter; };

template<typename T>
using CommitterFor = CommitterFromProps<std::remove_cvref_t<T>>::type;


class EaxEffect {
public:
    EaxEffect() noexcept = default;
    ~EaxEffect() = default;

    ALenum al_effect_type_{AL_EFFECT_NULL};
    EffectProps al_effect_props_;

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
    Props4 props_;
    State1 state1_{};
    State2 state2_{};
    State3 state3_{};
    State4 state4_{};
    State4 state5_{};


    static void call_set_defaults(const ALenum altype, EaxEffectProps &props)
    {
        switch(altype)
        {
        case AL_EFFECT_EAXREVERB: return EaxReverbCommitter::SetDefaults(props);
        case AL_EFFECT_CHORUS: return EaxChorusCommitter::SetDefaults(props);
        case AL_EFFECT_AUTOWAH: return EaxAutowahCommitter::SetDefaults(props);
        case AL_EFFECT_COMPRESSOR: return EaxCompressorCommitter::SetDefaults(props);
        case AL_EFFECT_DISTORTION: return EaxDistortionCommitter::SetDefaults(props);
        case AL_EFFECT_ECHO: return EaxEchoCommitter::SetDefaults(props);
        case AL_EFFECT_EQUALIZER: return EaxEqualizerCommitter::SetDefaults(props);
        case AL_EFFECT_FLANGER: return EaxFlangerCommitter::SetDefaults(props);
        case AL_EFFECT_FREQUENCY_SHIFTER: return EaxFrequencyShifterCommitter::SetDefaults(props);
        case AL_EFFECT_RING_MODULATOR: return EaxModulatorCommitter::SetDefaults(props);
        case AL_EFFECT_PITCH_SHIFTER: return EaxPitchShifterCommitter::SetDefaults(props);
        case AL_EFFECT_VOCAL_MORPHER: return EaxVocalMorpherCommitter::SetDefaults(props);
        case AL_EFFECT_NULL: break;
        }
        return EaxNullCommitter::SetDefaults(props);
    }

    template<typename T>
    void init()
    {
        EaxReverbCommitter::SetDefaults(state1_.d);
        state1_.i = state1_.d;
        EaxReverbCommitter::SetDefaults(state2_.d);
        state2_.i = state2_.d;
        EaxReverbCommitter::SetDefaults(state3_.d);
        state3_.i = state3_.d;
        T::SetDefaults(state4_.d);
        state4_.i = state4_.d;
        T::SetDefaults(state5_.d);
        state5_.i = state5_.d;
    }

    void set_defaults(int eax_version, ALenum altype)
    {
        switch(eax_version)
        {
        case 1: EaxReverbCommitter::SetDefaults(state1_.d); break;
        case 2: EaxReverbCommitter::SetDefaults(state2_.d); break;
        case 3: EaxReverbCommitter::SetDefaults(state3_.d); break;
        case 4: call_set_defaults(altype, state4_.d); break;
        case 5: call_set_defaults(altype, state5_.d); break;
        }
        changed_ = true;
    }


    static void call_set(const EaxCall &call, EaxEffectProps &props)
    {
        return std::visit([&]<typename T>(T &arg) { return CommitterFor<T>::Set(call, arg); },
            props);
    }

    void set(const EaxCall &call)
    {
        switch(call.get_version())
        {
        case 1: EaxReverbCommitter::Set(call, state1_.d); break;
        case 2: EaxReverbCommitter::Set(call, state2_.d); break;
        case 3: EaxReverbCommitter::Set(call, state3_.d); break;
        case 4: call_set(call, state4_.d); break;
        case 5: call_set(call, state5_.d); break;
        }
        changed_ = true;
    }


    static void call_get(const EaxCall &call, const EaxEffectProps &props)
    {
        return std::visit([&]<typename T>(T &arg) { return CommitterFor<T>::Get(call, arg); },
            props);
    }

    void get(const EaxCall &call) const
    {
        switch(call.get_version())
        {
        case 1: EaxReverbCommitter::Get(call, state1_.d); break;
        case 2: EaxReverbCommitter::Get(call, state2_.d); break;
        case 3: EaxReverbCommitter::Get(call, state3_.d); break;
        case 4: call_get(call, state4_.d); break;
        case 5: call_get(call, state5_.d); break;
        }
    }


    bool call_commit(const EaxEffectProps &props)
    {
        return std::visit([&]<typename T>(T &arg)
            { return CommitterFor<T>{props_, al_effect_props_}.commit(arg); },
            props);
    }

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
            ret |= EaxReverbCommitter{props_, al_effect_props_}.commit(state1_.d);
            break;
        case 2:
            state2_.i = state2_.d;
            ret |= EaxReverbCommitter{props_, al_effect_props_}.commit(state2_.d);
            break;
        case 3:
            state3_.i = state3_.d;
            ret |= EaxReverbCommitter{props_, al_effect_props_}.commit(state3_.d);
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
