#ifndef AL_AUXEFFECTSLOT_H
#define AL_AUXEFFECTSLOT_H

#include "config.h"

#include <array>
#include <atomic>
#include <bitset>
#include <concepts>
#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>

#include "AL/al.h"

#include "alnumeric.h"
#include "core/effects/base.h"
#include "core/effectslot.h"
#include "gsl/gsl"
#include "intrusive_ptr.h"

#if ALSOFT_EAX
#include <memory>
#include "eax/api.h"
#include "eax/call.h"
#include "eax/effect.h"
#include "eax/exception.h"
#include "eax/fx_slot_index.h"
#include "eax/utils.h"
#endif // ALSOFT_EAX


#if ALSOFT_EAX
/* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
class EaxFxSlotException final : public EaxException {
public:
    explicit EaxFxSlotException(const std::string_view message)
		: EaxException{"EAX_FX_SLOT", message}
    { }
};
#endif // ALSOFT_EAX

enum class SlotState : bool {
    Initial, Playing,
};

namespace al {

struct Context;
struct Buffer;

struct EffectSlot {
    u32 mEffectId{};
    f32 mGain{1.0f};
    bool mAuxSendAuto{true};
    al::intrusive_ptr<EffectSlot> mTarget;
    al::intrusive_ptr<al::Buffer> mBuffer;

    struct EffectData {
        EffectSlotType Type{EffectSlotType::None};
        EffectProps Props;

        al::intrusive_ptr<EffectState> State;
    };
    EffectData mEffect;

    bool mPropsDirty{true};

    SlotState mState{SlotState::Initial};

    std::atomic<ALuint> mRef{0u};

    gsl::not_null<EffectSlotBase*> mSlot;

    /* Self ID */
    u32 mId{};

    explicit EffectSlot(gsl::not_null<al::Context*> context);
    EffectSlot(const EffectSlot&) = delete;
    auto operator=(const EffectSlot&) -> EffectSlot& = delete;
    ~EffectSlot();

    auto inc_ref() noexcept { return mRef.fetch_add(1, std::memory_order_acq_rel)+1; }
    auto dec_ref() noexcept { return mRef.fetch_sub(1, std::memory_order_acq_rel)-1; }
    auto newReference() noexcept
    {
        mRef.fetch_add(1, std::memory_order_acq_rel);
        return al::intrusive_ptr{this};
    }

    auto initEffect(u32 effectId, ALenum effectType, const EffectProps &effectProps,
        gsl::not_null<Context*> context) -> void;
    void updateProps(gsl::not_null<Context*> context) const;

    static void SetName(gsl::not_null<Context*> context, u32 id, std::string_view name);

#if ALSOFT_EAX
    void eax_initialize(EaxFxSlotIndexValue index);

    [[nodiscard]]
    auto eax_get_index() const noexcept -> EaxFxSlotIndexValue { return mEaxFXSlotIndex; }
    [[nodiscard]]
    auto eax_get_eax_fx_slot() const noexcept -> const EAX50FXSLOTPROPERTIES& { return mEax; }

    // Returns `true` if all sources should be updated, or `false` otherwise.
    [[nodiscard]] auto eax_dispatch(const EaxCall &call) -> bool
    { return call.is_get() ? eax_get(call) : eax_set(call); }

    void eax_commit();

private:
    enum {
        eax_load_effect_dirty_bit,
        eax_volume_dirty_bit,
        eax_lock_dirty_bit,
        eax_flags_dirty_bit,
        eax_occlusion_dirty_bit,
        eax_occlusion_lf_ratio_dirty_bit,
        eax_dirty_bit_count
    };

    using Exception = EaxFxSlotException;

    struct Eax4State {
        EAX40FXSLOTPROPERTIES i; // Immediate.
    };

    struct Eax5State {
        EAX50FXSLOTPROPERTIES i; // Immediate.
    };

    struct EaxRangeValidator {
        template<typename TValue>
        void operator()(const std::string_view name, const TValue &value, const TValue &min_value,
            const TValue &max_value) const
        {
            eax_validate_range<Exception>(name, value, min_value, max_value);
        }
    };

    struct Eax4GuidLoadEffectValidator {
        void operator()(const GUID& guidLoadEffect) const
        {
            if (guidLoadEffect != EAX_NULL_GUID &&
                guidLoadEffect != EAX_REVERB_EFFECT &&
                guidLoadEffect != EAX_AGCCOMPRESSOR_EFFECT &&
                guidLoadEffect != EAX_AUTOWAH_EFFECT &&
                guidLoadEffect != EAX_CHORUS_EFFECT &&
                guidLoadEffect != EAX_DISTORTION_EFFECT &&
                guidLoadEffect != EAX_ECHO_EFFECT &&
                guidLoadEffect != EAX_EQUALIZER_EFFECT &&
                guidLoadEffect != EAX_FLANGER_EFFECT &&
                guidLoadEffect != EAX_FREQUENCYSHIFTER_EFFECT &&
                guidLoadEffect != EAX_VOCALMORPHER_EFFECT &&
                guidLoadEffect != EAX_PITCHSHIFTER_EFFECT &&
                guidLoadEffect != EAX_RINGMODULATOR_EFFECT)
            {
                eax_fail_unknown_effect_id();
            }
        }
    };

    struct Eax4VolumeValidator {
        void operator()(eax_long const lVolume) const
        {
            EaxRangeValidator{}(
                "Volume",
                lVolume,
                EAXFXSLOT_MINVOLUME,
                EAXFXSLOT_MAXVOLUME);
        }
    };

    struct Eax4LockValidator {
        void operator()(eax_long const lLock) const
        {
            EaxRangeValidator{}(
                "Lock",
                lLock,
                EAXFXSLOT_MINLOCK,
                EAXFXSLOT_MAXLOCK);
        }
    };

    struct Eax4FlagsValidator {
        void operator()(eax_ulong const ulFlags) const
        {
            EaxRangeValidator{}(
                "Flags",
                ulFlags,
                0_eax_ulong,
                ~EAX40FXSLOTFLAGS_RESERVED);
        }
    };

    struct Eax4AllValidator {
        void operator()(const EAX40FXSLOTPROPERTIES& all) const
        {
            Eax4GuidLoadEffectValidator{}(all.guidLoadEffect);
            Eax4VolumeValidator{}(all.lVolume);
            Eax4LockValidator{}(all.lLock);
            Eax4FlagsValidator{}(all.ulFlags);
        }
    };

    struct Eax5FlagsValidator {
        void operator()(eax_ulong const ulFlags) const
        {
            EaxRangeValidator{}(
                "Flags",
                ulFlags,
                0_eax_ulong,
                ~EAX50FXSLOTFLAGS_RESERVED);
        }
    };

    struct Eax5OcclusionValidator {
        void operator()(eax_long const lOcclusion) const
        {
            EaxRangeValidator{}(
                "Occlusion",
                lOcclusion,
                EAXFXSLOT_MINOCCLUSION,
                EAXFXSLOT_MAXOCCLUSION);
        }
    };

    struct Eax5OcclusionLfRatioValidator {
        void operator()(float const flOcclusionLFRatio) const
        {
            EaxRangeValidator{}(
                "Occlusion LF Ratio",
                flOcclusionLFRatio,
                EAXFXSLOT_MINOCCLUSIONLFRATIO,
                EAXFXSLOT_MAXOCCLUSIONLFRATIO);
        }
    };

    struct Eax5AllValidator {
        void operator()(const EAX50FXSLOTPROPERTIES& all) const
        {
            Eax4GuidLoadEffectValidator{}(all.guidLoadEffect);
            Eax4VolumeValidator{}(all.lVolume);
            Eax4LockValidator{}(all.lLock);
            Eax5FlagsValidator{}(all.ulFlags);
            Eax5OcclusionValidator{}(all.lOcclusion);
            Eax5OcclusionLfRatioValidator{}(all.flOcclusionLFRatio);
        }
    };

    gsl::not_null<Context*> const mEaxALContext;
    EaxFxSlotIndexValue mEaxFXSlotIndex{};
    int mEaxVersion{}; // Current EAX version.
    std::bitset<eax_dirty_bit_count> mEaxDf; // Dirty flags for the current EAX version.
    EaxEffectUPtr mEaxEffect;
    Eax5State mEax123{}; // EAX1/EAX2/EAX3 state.
    Eax4State mEax4{}; // EAX4 state.
    Eax5State mEax5{}; // EAX5 state.
    EAX50FXSLOTPROPERTIES mEax{}; // Current EAX state.

    [[noreturn]] static void eax_fail(std::string_view message);
    [[noreturn]] static void eax_fail_unknown_effect_id();
    [[noreturn]] static void eax_fail_unknown_property_id();
    [[noreturn]] static void eax_fail_unknown_version();

    /* Gets a new value from EAX call, validates it, sets a dirty flag only if
     * the new value differs form the old one, and assigns the new value.
     */
    template<typename TValidator>
    void eax_fx_slot_set(const EaxCall &call, auto &dst, size_t dirty_bit)
    {
        const auto &src = call.load<const std::remove_cvref_t<decltype(dst)>>();
        TValidator{}(src);
        if(dst != src)
        {
            mEaxDf.set(dirty_bit);
            dst = src;
        }
    }

    /* Gets a new value from EAX call, validates it, sets a dirty flag without
     * comparing the values, and assigns the new value.
     */
    template<typename TValidator>
    void eax_fx_slot_set_dirty(const EaxCall &call, auto &dst, size_t dirty_bit)
    {
        const auto &src = call.load<const std::remove_cvref_t<decltype(dst)>>();
        TValidator{}(src);
        mEaxDf.set(dirty_bit);
        dst = src;
    }

    [[nodiscard]] constexpr auto eax4_fx_slot_is_legacy() const noexcept -> bool
    { return mEaxFXSlotIndex < 2; }

    void eax4_fx_slot_ensure_unlocked() const;

    [[nodiscard]] static auto eax_get_efx_effect_type(const GUID& guid) -> ALenum;
    [[nodiscard]] auto eax_get_eax_default_effect_guid() const noexcept -> const GUID&;
    [[nodiscard]] auto eax_get_eax_default_lock() const noexcept -> eax_long;

    void eax4_fx_slot_set_defaults(EAX40FXSLOTPROPERTIES& props) const noexcept;
    void eax5_fx_slot_set_defaults(EAX50FXSLOTPROPERTIES& props) const noexcept;
    void eax_fx_slot_set_defaults();

    static void eax4_fx_slot_get(const EaxCall& call, const EAX40FXSLOTPROPERTIES& props);
    static void eax5_fx_slot_get(const EaxCall& call, const EAX50FXSLOTPROPERTIES& props);
    void eax_fx_slot_get(const EaxCall& call) const;
    // Returns `true` if all sources should be updated, or `false` otherwise.
    [[nodiscard]]
    auto eax_get(const EaxCall& call) const -> bool;

    void eax_fx_slot_load_effect(int version, ALenum altype) const;
    void eax_fx_slot_set_volume();
    void eax_fx_slot_set_environment_flag();
    void eax_fx_slot_set_flags();

    void eax4_fx_slot_set_all(const EaxCall& call);
    void eax5_fx_slot_set_all(const EaxCall& call);

    [[nodiscard]] auto eax_fx_slot_should_update_sources() const noexcept -> bool;

    // Returns `true` if all sources should be updated, or `false` otherwise.
    bool eax4_fx_slot_set(const EaxCall& call);
    // Returns `true` if all sources should be updated, or `false` otherwise.
    bool eax5_fx_slot_set(const EaxCall& call);
    // Returns `true` if all sources should be updated, or `false` otherwise.
    bool eax_fx_slot_set(const EaxCall& call);
    // Returns `true` if all sources should be updated, or `false` otherwise.
    bool eax_set(const EaxCall& call);

    void eax_fx_slot_commit_property(auto &state, std::bitset<eax_dirty_bit_count> &dst_df,
        size_t dirty_bit, std::invocable<decltype(mEax)> auto member) noexcept
    {
        if(mEaxDf.test(dirty_bit))
        {
            dst_df.set(dirty_bit);
            std::invoke(member, mEax) = std::invoke(member, state.i);
        }
    }

    void eax4_fx_slot_commit(std::bitset<eax_dirty_bit_count>& dst_df);
    void eax5_fx_slot_commit(Eax5State& state, std::bitset<eax_dirty_bit_count>& dst_df);

    // `alAuxiliaryEffectSloti(effect_slot, AL_EFFECTSLOT_EFFECT, effect)`
    void eax_set_efx_slot_effect(EaxEffect const &effect);

    // `alAuxiliaryEffectSloti(effect_slot, AL_EFFECTSLOT_AUXILIARY_SEND_AUTO, value)`
    void eax_set_efx_slot_send_auto(bool is_send_auto);

    // `alAuxiliaryEffectSlotf(effect_slot, AL_EFFECTSLOT_GAIN, gain)`
    void eax_set_efx_slot_gain(f32 gain);

public:
    class EaxDeleter {
    public:
        void operator()(gsl::not_null<EffectSlot*> effect_slot) const;
    };
#endif // ALSOFT_EAX
};

} /* namespace al */

void UpdateAllEffectSlotProps(gsl::not_null<al::Context*> context);

#if ALSOFT_EAX
using EaxAlEffectSlotUPtr = std::unique_ptr<al::EffectSlot, al::EffectSlot::EaxDeleter>;

auto eax_create_al_effect_slot(gsl::not_null<al::Context*> context) -> EaxAlEffectSlotUPtr;
#endif // ALSOFT_EAX

struct EffectSlotSubList {
    u64 mFreeMask{~0_u64};
    gsl::owner<std::array<al::EffectSlot,64>*> mEffectSlots{nullptr};

    EffectSlotSubList() noexcept = default;
    EffectSlotSubList(const EffectSlotSubList&) = delete;
    EffectSlotSubList(EffectSlotSubList&& rhs) noexcept
      : mFreeMask{rhs.mFreeMask}, mEffectSlots{rhs.mEffectSlots}
    { rhs.mFreeMask = ~0_u64; rhs.mEffectSlots = nullptr; }
    ~EffectSlotSubList();

    EffectSlotSubList& operator=(const EffectSlotSubList&) = delete;
    EffectSlotSubList& operator=(EffectSlotSubList&& rhs) noexcept
    { std::swap(mFreeMask, rhs.mFreeMask); std::swap(mEffectSlots, rhs.mEffectSlots); return *this; }
};

#endif
