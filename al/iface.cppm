module;

#include "al/auxeffectslot.h"
#include "al/buffer.h"
#include "al/debug.h"
#include "al/effect.h"
#include "al/event.h"
#include "al/filter.h"
#include "al/listener.h"
#include "al/source.h"

export module al.iface;

export import core.effectslot;
export import core.buffer_storage;

export {

    using ::SlotState;

    namespace al {
        using al::EffectSlot;
        using al::Buffer;
        using al::Effect;
        using al::Filter;
        using al::Listener;
        using al::BufferQueueItem;
        using al::Source;
    }

    using ::UpdateAllEffectSlotProps;

#if ALSOFT_EAX
    using ::EaxAlEffectSlotUPtr;
    using ::eax_create_al_effect_slot;

    using ::EaxStorage;

    using ::EaxAlLowPassParam;
#endif // ALSOFT_EAX

    using ::EffectSlotSubList;
    using ::BufferSubList;
    using ::EffectSubList;
    using ::FilterSubList;
    using ::SourceSubList;


    using ::MaxDebugLoggedMessages;
    using ::MaxDebugMessageLength;
    using ::MaxDebugGroupDepth;
    using ::MaxObjectLabelLength;

    using ::DebugSourceBase;
    using ::DebugSource;
    using ::DebugSourceCount;
    using ::DebugTypeBase;
    using ::DebugType;
    using ::DebugTypeCount;
    using ::DebugSeverityBase;
    using ::DebugSeverity;
    using ::DebugSeverityCount;

    using ::DebugGroup;


    using ::EffectTypeFlag;
    using ::DisabledEffects;

    using ::EffectList;
    using ::gEffectList;

    using ::EffectHandlerVariant;

    using ::InitEffect;

    using ::LoadReverbPreset;

    using ::IsValidEffectType;


    using ::StartEventThrd;
    using ::StopEventThrd;


    using ::LowPassFreqRef;
    using ::HighPassFreqRef;
    using ::FilterTable;
    using ::NullFilterTable;
    using ::LowpassFilterTable;
    using ::HighpassFilterTable;
    using ::BandpassFilterTable;


    using ::SourceStereo;

    using ::DefaultSendCount;
    using ::InvalidVoiceIndex;

    using ::sBufferSubDataCompat;

    using ::UpdateAllSourceProps;
}
