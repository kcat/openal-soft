
#include "config.h"

#include <cassert>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

#include "async_event.h"
#include "context.h"
#include "device.h"
#include "effectslot.h"
#include "logging.h"
#include "ringbuffer.h"
#include "voice.h"
#include "voice_change.h"


#ifdef __cpp_lib_atomic_is_always_lock_free
static_assert(std::atomic<ContextBase::AsyncEventBitset>::is_always_lock_free, "atomic<bitset> isn't lock-free");
#endif

ContextBase::ContextBase(DeviceBase *device) : mDevice{device}
{ assert(mEnabledEvts.is_lock_free()); }

ContextBase::~ContextBase()
{
    size_t count{0};
    ContextProps *cprops{mParams.ContextUpdate.exchange(nullptr, std::memory_order_relaxed)};
    if(std::unique_ptr<ContextProps> old{cprops})
        ++count;

    cprops = mFreeContextProps.exchange(nullptr, std::memory_order_acquire);
    while(std::unique_ptr<ContextProps> old{cprops})
    {
        cprops = old->next.load(std::memory_order_relaxed);
        ++count;
    }
    TRACE("Freed %zu context property object%s\n", count, (count==1)?"":"s");

    count = 0;
    EffectSlotProps *eprops{mFreeEffectslotProps.exchange(nullptr, std::memory_order_acquire)};
    while(std::unique_ptr<EffectSlotProps> old{eprops})
    {
        eprops = old->next.load(std::memory_order_relaxed);
        ++count;
    }
    TRACE("Freed %zu AuxiliaryEffectSlot property object%s\n", count, (count==1)?"":"s");

    if(std::unique_ptr<EffectSlotArray> curarray{mActiveAuxSlots.exchange(nullptr, std::memory_order_relaxed)})
        std::destroy_n(curarray->end(), curarray->size());

    std::unique_ptr<ContextBase::VoiceArray>{mVoices.exchange(nullptr, std::memory_order_relaxed)};

    if(mAsyncEvents)
    {
        count = 0;
        auto evt_vec = mAsyncEvents->getReadVector();
        if(evt_vec.first.len > 0)
        {
            std::destroy_n(std::launder(reinterpret_cast<AsyncEvent*>(evt_vec.first.buf)),
                evt_vec.first.len);
            count += evt_vec.first.len;
        }
        if(evt_vec.second.len > 0)
        {
            std::destroy_n(std::launder(reinterpret_cast<AsyncEvent*>(evt_vec.second.buf)),
                evt_vec.second.len);
            count += evt_vec.second.len;
        }
        if(count > 0)
            TRACE("Destructed %zu orphaned event%s\n", count, (count==1)?"":"s");
        mAsyncEvents->readAdvance(count);
    }
}


void ContextBase::allocVoiceChanges()
{
    static constexpr size_t clustersize{std::tuple_size_v<VoiceChangeCluster::element_type>};

    VoiceChangeCluster clusterptr{std::make_unique<VoiceChangeCluster::element_type>()};
    const auto cluster = al::span{*clusterptr};

    for(size_t i{1};i < clustersize;++i)
        cluster[i-1].mNext.store(std::addressof(cluster[i]), std::memory_order_relaxed);
    cluster[clustersize-1].mNext.store(mVoiceChangeTail, std::memory_order_relaxed);

    mVoiceChangeClusters.emplace_back(std::move(clusterptr));
    mVoiceChangeTail = mVoiceChangeClusters.back()->data();
}

void ContextBase::allocVoiceProps()
{
    static constexpr size_t clustersize{std::tuple_size_v<VoicePropsCluster::element_type>};

    TRACE("Increasing allocated voice properties to %zu\n",
        (mVoicePropClusters.size()+1) * clustersize);

    auto clusterptr = std::make_unique<VoicePropsCluster::element_type>();
    auto cluster = al::span{*clusterptr};
    for(size_t i{1};i < clustersize;++i)
        cluster[i-1].next.store(std::addressof(cluster[i]), std::memory_order_relaxed);
    mVoicePropClusters.emplace_back(std::move(clusterptr));

    VoicePropsItem *oldhead{mFreeVoiceProps.load(std::memory_order_acquire)};
    do {
        mVoicePropClusters.back()->back().next.store(oldhead, std::memory_order_relaxed);
    } while(mFreeVoiceProps.compare_exchange_weak(oldhead, mVoicePropClusters.back()->data(),
        std::memory_order_acq_rel, std::memory_order_acquire) == false);
}

void ContextBase::allocVoices(size_t addcount)
{
    static constexpr size_t clustersize{std::tuple_size_v<VoiceCluster::element_type>};
    /* Convert element count to cluster count. */
    addcount = (addcount+(clustersize-1)) / clustersize;

    if(!addcount)
    {
        if(!mVoiceClusters.empty())
            return;
        ++addcount;
    }

    if(addcount >= std::numeric_limits<int>::max()/clustersize - mVoiceClusters.size())
        throw std::runtime_error{"Allocating too many voices"};
    const size_t totalcount{(mVoiceClusters.size()+addcount) * clustersize};
    TRACE("Increasing allocated voices to %zu\n", totalcount);

    while(addcount)
    {
        mVoiceClusters.emplace_back(std::make_unique<VoiceCluster::element_type>());
        --addcount;
    }

    auto newarray = VoiceArray::Create(totalcount);
    auto voice_iter = newarray->begin();
    for(VoiceCluster &cluster : mVoiceClusters)
        voice_iter = std::transform(cluster->begin(), cluster->end(), voice_iter,
            [](Voice &voice) noexcept -> Voice* { return &voice; });

    if(std::unique_ptr<ContextBase::VoiceArray> oldvoices{mVoices.exchange(newarray.release(), std::memory_order_acq_rel)})
        std::ignore = mDevice->waitForMix();
}


EffectSlot *ContextBase::getEffectSlot()
{
    for(auto& clusterptr : mEffectSlotClusters)
    {
        const auto cluster = al::span{*clusterptr};
        auto iter = std::find_if_not(cluster.begin(), cluster.end(),
            std::mem_fn(&EffectSlot::InUse));
        if(iter != cluster.end()) return al::to_address(iter);
    }

    auto clusterptr = std::make_unique<EffectSlotCluster::element_type>();
    if(1 >= std::numeric_limits<int>::max()/clusterptr->size() - mEffectSlotClusters.size())
        throw std::runtime_error{"Allocating too many effect slots"};
    const size_t totalcount{(mEffectSlotClusters.size()+1) * clusterptr->size()};
    TRACE("Increasing allocated effect slots to %zu\n", totalcount);

    mEffectSlotClusters.emplace_back(std::move(clusterptr));
    return mEffectSlotClusters.back()->data();
}
