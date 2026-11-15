#include "PlanExchange.h"

#include <algorithm>

namespace artfreq
{

// Why this is safe without locks:
//
//  - `pending` only ever holds the newest published plan (or nothing). acquire() takes it with
//    an atomic exchange, so the audio thread only ever moves to newer plans, and a plan that
//    publish() takes back out of `pending` was never seen by the audio thread.
//  - Between acquire() and reportInUse() the audio thread holds a plan it has not reported yet.
//    That plan is newer than everything it reported before, so it is never older than the
//    reported versions, and the garbage collector leaves it alone.
//  - The audio thread reports the OLDER of its two plans as one atomic value, so the collector
//    can never see half of a report (a new current version with a stale fading one).

static_assert (std::atomic<ArtPlan*>::is_always_lock_free && std::atomic<MaskPlan*>::is_always_lock_free
                   && std::atomic<MaskPlanSet*>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free,
               "the audio thread must never wait for the exchange");

template <typename Plan>
VersionedExchange<Plan>::~VersionedExchange() = default;

template <typename Plan>
void VersionedExchange<Plan>::publish (std::shared_ptr<Plan> plan)
{
    if (plan == nullptr)
        return;

    plan->version = nextVersion++;
    alive.push_back ({ plan, false });

    // Release: the audio thread sees the finished plan once it sees the pointer.
    auto* replaced = pending.exchange (plan.get(), std::memory_order_acq_rel);

    if (replaced != nullptr)
        for (auto& kept : alive)
            if (kept.plan.get() == replaced)
                kept.skipped = true;
}

template <typename Plan>
std::shared_ptr<const Plan> VersionedExchange<Plan>::latest() const
{
    if (alive.empty())
        return {};

    return alive.back().plan;
}

template <typename Plan>
void VersionedExchange<Plan>::collectGarbage()
{
    if (alive.size() < 2)
        return;

    const auto reported = audioHasReported.load (std::memory_order_acquire);
    const auto oldest = oldestInUse.load (std::memory_order_acquire);
    const auto* newest = alive.back().plan.get();

    const auto unreachable = [&] (const Kept& kept)
    {
        if (kept.plan.get() == newest)
            return false;

        return kept.skipped || (reported && kept.plan->version < oldest);
    };

    alive.erase (std::remove_if (alive.begin(), alive.end(), unreachable), alive.end());
}

template <typename Plan>
const Plan* VersionedExchange<Plan>::acquire() noexcept
{
    // Acquire: everything the message thread wrote into the plan is visible from here on.
    return pending.exchange (nullptr, std::memory_order_acq_rel);
}

template <typename Plan>
void VersionedExchange<Plan>::reportInUse (uint64_t currentVersion, uint64_t fadingVersion) noexcept
{
    // The older of the two (0 means "none"). The audio thread only moves forward, so this never
    // goes down; a report of no plans at all keeps the previous value rather than claiming that
    // nothing could be in flight.
    auto oldest = currentVersion;

    if (fadingVersion != 0 && (oldest == 0 || fadingVersion < oldest))
        oldest = fadingVersion;

    if (oldest > oldestInUse.load (std::memory_order_relaxed))
        oldestInUse.store (oldest, std::memory_order_release);

    audioHasReported.store (true, std::memory_order_release);
}

template class VersionedExchange<ArtPlan>;
template class VersionedExchange<MaskPlan>;
template class VersionedExchange<MaskPlanSet>;

} // namespace artfreq
