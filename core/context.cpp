
#include "config.h"

#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>

#include "context.h"
#include "device.h"
#include "effectslot.h"
#include "gsl/gsl"
#include "logging.h"
#include "ringbuffer.h"
#include "voice.h"
#include "voice_change.h"


#ifdef __cpp_lib_atomic_is_always_lock_free
static_assert(std::atomic<ContextBase::AsyncEventBitset>::is_always_lock_free, "atomic<bitset> isn't lock-free");
#endif

ContextBase::ContextBase(gsl::not_null<DeviceBase*> device) : mDevice{device}
{ Expects(mEnabledEvts.is_lock_free()); }

ContextBase::~ContextBase()
{
    mActiveAuxSlots.store(nullptr, std::memory_order_relaxed);
    mVoices.store(nullptr, std::memory_order_relaxed);

    if(mAsyncEvents)
    {
        if(const auto count = mAsyncEvents->readSpace(); count > 0)
            TRACE("Destructing {} orphaned event{}", count, (count==1)?"":"s");
        mAsyncEvents = nullptr;
    }
}


void ContextBase::allocVoiceChanges()
{
    static constexpr auto clustersize = std::tuple_size_v<VoiceChangeCluster::element_type>;

    auto clusterptr = std::make_unique<VoiceChangeCluster::element_type>();
    const auto cluster = std::span{*clusterptr};

    for(const auto i : std::views::iota(1_uz, clustersize))
        cluster[i-1].mNext.store(std::addressof(cluster[i]), std::memory_order_relaxed);
    cluster[clustersize-1].mNext.store(mVoiceChangeTail, std::memory_order_relaxed);

    mVoiceChangeClusters.emplace_back(std::move(clusterptr));
    mVoiceChangeTail = mVoiceChangeClusters.back()->data();
}

void ContextBase::allocVoiceProps()
{
    static constexpr size_t clustersize{std::tuple_size_v<VoicePropsCluster::element_type>};

    TRACE("Increasing allocated voice properties to {}",
        (mVoicePropClusters.size()+1) * clustersize);

    auto clusterptr = std::make_unique<VoicePropsCluster::element_type>();
    auto cluster = std::span{*clusterptr};
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
    TRACE("Increasing allocated voices to {}", totalcount);

    while(addcount)
    {
        mVoiceClusters.emplace_back(std::make_unique<VoiceCluster::element_type>());
        --addcount;
    }

    auto newarray = VoiceArray::Create(totalcount);
    auto voice_iter = newarray->begin();
    for(VoiceCluster &cluster : mVoiceClusters)
        voice_iter = std::ranges::transform(*cluster, voice_iter,
            [](Voice &voice) noexcept -> Voice* { return std::addressof(voice); }).out;

    if(auto oldvoices = mVoices.exchange(std::move(newarray), std::memory_order_acq_rel))
        std::ignore = mDevice->waitForMix();
}


void ContextBase::allocEffectSlotProps()
{
    static constexpr size_t clustersize{std::tuple_size_v<EffectSlotPropsCluster::element_type>};

    TRACE("Increasing allocated effect slot properties to {}",
        (mEffectSlotPropClusters.size()+1) * clustersize);

    auto clusterptr = std::make_unique<EffectSlotPropsCluster::element_type>();
    auto cluster = std::span{*clusterptr};
    for(size_t i{1};i < clustersize;++i)
        cluster[i-1].next.store(std::addressof(cluster[i]), std::memory_order_relaxed);
    auto *newcluster = mEffectSlotPropClusters.emplace_back(std::move(clusterptr)).get();

    EffectSlotProps *oldhead{mFreeEffectSlotProps.load(std::memory_order_acquire)};
    do {
        newcluster->back().next.store(oldhead, std::memory_order_relaxed);
    } while(mFreeEffectSlotProps.compare_exchange_weak(oldhead, newcluster->data(),
        std::memory_order_acq_rel, std::memory_order_acquire) == false);
}

auto ContextBase::getEffectSlot() -> gsl::not_null<EffectSlot*>
{
    for(auto &clusterptr : mEffectSlotClusters)
    {
        const auto cluster = std::span{*clusterptr};
        if(const auto iter = std::ranges::find_if_not(cluster, &EffectSlot::InUse);
            iter != cluster.end())
            return gsl::make_not_null(std::to_address(iter));
    }

    auto clusterptr = std::make_unique<EffectSlotCluster::element_type>();
    if(1 >= std::numeric_limits<int>::max()/clusterptr->size() - mEffectSlotClusters.size())
        throw std::runtime_error{"Allocating too many effect slots"};
    const size_t totalcount{(mEffectSlotClusters.size()+1) * clusterptr->size()};
    TRACE("Increasing allocated effect slots to {}", totalcount);

    mEffectSlotClusters.emplace_back(std::move(clusterptr));
    return gsl::make_not_null(mEffectSlotClusters.back()->data());
}


void ContextBase::allocContextProps()
{
    static constexpr size_t clustersize{std::tuple_size_v<ContextPropsCluster::element_type>};

    TRACE("Increasing allocated context properties to {}",
        (mContextPropClusters.size()+1) * clustersize);

    auto clusterptr = std::make_unique<ContextPropsCluster::element_type>();
    auto cluster = std::span{*clusterptr};
    for(size_t i{1};i < clustersize;++i)
        cluster[i-1].next.store(std::addressof(cluster[i]), std::memory_order_relaxed);
    auto *newcluster = mContextPropClusters.emplace_back(std::move(clusterptr)).get();

    ContextProps *oldhead{mFreeContextProps.load(std::memory_order_acquire)};
    do {
        newcluster->back().next.store(oldhead, std::memory_order_relaxed);
    } while(mFreeContextProps.compare_exchange_weak(oldhead, newcluster->data(),
        std::memory_order_acq_rel, std::memory_order_acquire) == false);
}
