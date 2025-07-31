
#include "config.h"

#include "event.h"

#include <atomic>
#include <bitset>
#include <exception>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <variant>

#include "AL/al.h"
#include "AL/alext.h"

#include "alc/context.h"
#include "alnumeric.h"
#include "alstring.h"
#include "core/async_event.h"
#include "core/context.h"
#include "core/effects/base.h"
#include "core/except.h"
#include "core/logging.h"
#include "direct_defs.h"
#include "fmt/core.h"
#include "intrusive_ptr.h"
#include "opthelpers.h"
#include "ringbuffer.h"


namespace {

using namespace std::string_view_literals;

template<typename... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

template<typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

auto EventThread(al::Context *context) -> void
{
    auto *ring = context->mAsyncEvents.get();
    auto quitnow = false;
    while(!quitnow)
    {
        auto evt_span = ring->getReadVector()[0];
        if(evt_span.empty())
        {
            context->mEventsPending.wait(false, std::memory_order_acquire);
            context->mEventsPending.store(false, std::memory_order_release);
            continue;
        }

        auto eventlock = std::lock_guard{context->mEventCbLock};
        const auto enabledevts = context->mEnabledEvts.load(std::memory_order_acquire);
        for(auto &event : evt_span)
        {
            quitnow = std::holds_alternative<AsyncKillThread>(event);
            if(quitnow) [[unlikely]] break;

            std::visit(overloaded {
                [](AsyncKillThread&) { },
                [](AsyncEffectReleaseEvent &evt)
                {
                    al::intrusive_ptr<EffectState>{evt.mEffectState};
                },
                [context,enabledevts](AsyncSourceStateEvent &evt)
                {
                    if(!context->mEventCb
                        || !enabledevts.test(al::to_underlying(AsyncEnableBits::SourceState)))
                        return;

                    auto state = ALuint{};
                    auto state_sv = std::string_view{};
                    switch(evt.mState)
                    {
                    case AsyncSrcState::Reset:
                        state_sv = "AL_INITIAL"sv;
                        state = AL_INITIAL;
                        break;
                    case AsyncSrcState::Stop:
                        state_sv = "AL_STOPPED"sv;
                        state = AL_STOPPED;
                        break;
                    case AsyncSrcState::Play:
                        state_sv = "AL_PLAYING"sv;
                        state = AL_PLAYING;
                        break;
                    case AsyncSrcState::Pause:
                        state_sv = "AL_PAUSED"sv;
                        state = AL_PAUSED;
                        break;
                    }

                    const auto msg = fmt::format("Source ID {} state has changed to {}", evt.mId,
                        state_sv);
                    context->mEventCb(AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT, evt.mId, state,
                        al::sizei(msg), msg.c_str(), context->mEventParam);
                },
                [context,enabledevts](AsyncBufferCompleteEvent &evt)
                {
                    if(!context->mEventCb
                        || !enabledevts.test(al::to_underlying(AsyncEnableBits::BufferCompleted)))
                        return;

                    const auto msg = fmt::format("{} buffer{} completed", evt.mCount,
                        (evt.mCount == 1) ? "" : "s");
                    context->mEventCb(AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT, evt.mId, evt.mCount,
                        al::sizei(msg), msg.c_str(), context->mEventParam);
                },
                [context,enabledevts](AsyncDisconnectEvent &evt)
                {
                    if(!context->mEventCb
                        || !enabledevts.test(al::to_underlying(AsyncEnableBits::Disconnected)))
                        return;

                    context->mEventCb(AL_EVENT_TYPE_DISCONNECTED_SOFT, 0, 0, al::sizei(evt.msg),
                        evt.msg.c_str(), context->mEventParam);
                }
            }, event);
        }
        ring->readAdvance(evt_span.size());
    }
}

constexpr auto GetEventType(ALenum etype) noexcept -> std::optional<AsyncEnableBits>
{
    switch(etype)
    {
    case AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT: return AsyncEnableBits::BufferCompleted;
    case AL_EVENT_TYPE_DISCONNECTED_SOFT: return AsyncEnableBits::Disconnected;
    case AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT: return AsyncEnableBits::SourceState;
    }
    return std::nullopt;
}


void alEventControlSOFT(gsl::strict_not_null<al::Context*> context, ALsizei count,
    const ALenum *types, ALboolean enable) noexcept
try {
    if(count < 0)
        context->throw_error(AL_INVALID_VALUE, "Controlling {} events", count);
    if(count <= 0) [[unlikely]] return;

    if(!types)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    auto flags = ContextBase::AsyncEventBitset{};
    std::ranges::for_each(std::views::counted(types, count), [context,&flags](const ALenum evttype)
    {
        const auto etype = GetEventType(evttype);
        if(!etype)
            context->throw_error(AL_INVALID_ENUM, "Invalid event type {:#04x}",
                as_unsigned(evttype));
        flags.set(al::to_underlying(*etype));
    });

    if(enable)
    {
        auto enabledevts = context->mEnabledEvts.load(std::memory_order_relaxed);
        while(context->mEnabledEvts.compare_exchange_weak(enabledevts, enabledevts|flags,
            std::memory_order_acq_rel, std::memory_order_acquire) == 0)
        {
            /* enabledevts is (re-)filled with the current value on failure, so
             * just try again.
             */
        }
    }
    else
    {
        auto enabledevts = context->mEnabledEvts.load(std::memory_order_relaxed);
        while(context->mEnabledEvts.compare_exchange_weak(enabledevts, enabledevts&~flags,
            std::memory_order_acq_rel, std::memory_order_acquire) == 0)
        {
        }
        /* Wait to ensure the event handler sees the changed flags before
         * returning.
         */
        std::ignore = std::lock_guard{context->mEventCbLock};
    }
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alEventCallbackSOFT(gsl::strict_not_null<al::Context*> context, ALEVENTPROCSOFT callback,
    void *userParam) noexcept
try {
    auto eventlock = std::lock_guard{context->mEventCbLock};
    context->mEventCb = callback;
    context->mEventParam = userParam;
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

} // namespace


void StartEventThrd(al::Context *ctx)
{
    try {
        ctx->mEventThread = std::thread{EventThread, ctx};
    }
    catch(std::exception& e) {
        ERR("Failed to start event thread: {}", e.what());
    }
    catch(...) {
        ERR("Failed to start event thread! Expect problems.");
    }
}

void StopEventThrd(al::Context *ctx)
{
    auto *ring = ctx->mAsyncEvents.get();
    auto evt_span = ring->getWriteVector()[0];
    if(evt_span.empty())
    {
        do {
            std::this_thread::yield();
            evt_span = ring->getWriteVector()[0];
        } while(evt_span.empty());
    }
    std::ignore = InitAsyncEvent<AsyncKillThread>(evt_span[0]);
    ring->writeAdvance(1);

    if(ctx->mEventThread.joinable())
    {
        ctx->mEventsPending.store(true, std::memory_order_release);
        ctx->mEventsPending.notify_all();
        ctx->mEventThread.join();
    }
}

AL_API DECL_FUNCEXT3(void, alEventControl,SOFT, ALsizei,count, const ALenum*,types, ALboolean,enable)
AL_API DECL_FUNCEXT2(void, alEventCallback,SOFT, ALEVENTPROCSOFT,callback, void*,userParam)
