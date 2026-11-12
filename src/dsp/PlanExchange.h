#pragma once

// Hands plans from the message thread to the audio thread without locks or allocations on the
// audio thread, and frees them on the message thread once the audio thread is done with them.
//
// The message thread publishes plans with strictly increasing versions. The audio thread picks
// up the newest one with acquire() and reports which plans it still uses (the current one and,
// during a crossfade, the previous one) with reportInUse(). Because the audio thread only ever
// moves to newer plans, any plan older than both of the versions it reported can never be
// touched again, and collectGarbage() releases it.

#include "core/ArtPlan.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace freqart
{

class PlanExchange
{
public:
    PlanExchange() = default;
    ~PlanExchange();

    // ---- message thread ------------------------------------------------------------------

    /** Assigns the next version number to the plan, keeps it alive, and makes it the plan the
        audio thread picks up next (replacing an earlier published plan it has not picked up). */
    void publish (std::shared_ptr<ArtPlan> plan);

    /** The most recently published plan (null before the first). */
    std::shared_ptr<const ArtPlan> latest() const;

    /** Releases plans the audio thread can no longer reach. Call regularly (e.g. from a timer). */
    void collectGarbage();

    /** Number of plans currently kept alive (for tests). */
    size_t numAlive() const noexcept { return alive.size(); }

    // ---- audio thread --------------------------------------------------------------------

    /** The newest published plan not handed out before, or nullptr if there is none.
        Wait-free. The pointer stays valid until reportInUse has been called with versions
        that are both newer than the plan's. */
    const ArtPlan* acquire() noexcept;

    /** The versions of the plans the audio thread still reads: its current plan and the one
        it is fading out from (0 for none). Call after every acquire() that returned a plan,
        and whenever a crossfade ends. */
    void reportInUse (uint64_t currentVersion, uint64_t fadingVersion) noexcept;

private:
    std::vector<std::shared_ptr<ArtPlan>> alive;  // message thread only
    std::atomic<ArtPlan*> pending { nullptr };
    std::atomic<uint64_t> inUseCurrent { 0 };
    std::atomic<uint64_t> inUseFading { 0 };
    std::atomic<bool> audioHasReported { false };
    uint64_t nextVersion = 1;
};

} // namespace freqart
