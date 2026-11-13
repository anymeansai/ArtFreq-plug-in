// PlanExchange: plans handed from the message thread to the audio thread without locks, and
// freed once the audio thread can no longer reach them.

#include "TestFramework.h"

#include "dsp/PlanExchange.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using freqart::ArtPlan;
using freqart::PlanExchange;

namespace
{

/** Counts plans that are alive and remembers, by version, which ones have been deleted. */
struct Census
{
    explicit Census (size_t maxVersion)
        : deleted (new std::atomic<bool>[maxVersion + 1]()), size (maxVersion + 1) {}

    std::shared_ptr<ArtPlan> make (int tag = 0)
    {
        ++alive;
        auto* plan = new ArtPlan();
        plan->numColumns = tag;

        return std::shared_ptr<ArtPlan> (plan, [this] (ArtPlan* p)
        {
            if (p->version < size)
                deleted[p->version].store (true);

            --alive;
            delete p;
        });
    }

    bool isDeleted (uint64_t version) const { return version < size && deleted[version].load(); }

    std::atomic<int> alive { 0 };
    std::unique_ptr<std::atomic<bool>[]> deleted;
    size_t size;
};

} // namespace

FREQART_TEST (exchangeVersionsIncrease)
{
    Census census (16);

    {
        PlanExchange exchange;
        CHECK (exchange.latest() == nullptr);

        auto a = census.make(), b = census.make(), c = census.make();
        exchange.publish (a);
        exchange.publish (b);
        exchange.publish (c);
        exchange.publish (nullptr);   // ignored

        CHECK (a->version == 1);
        CHECK (b->version == 2);
        CHECK (c->version == 3);
        CHECK (exchange.latest().get() == c.get());
        CHECK (exchange.numAlive() == 3);
    }

    CHECK (census.alive.load() == 0);
}

FREQART_TEST (exchangeAcquireReturnsNewestOnce)
{
    Census census (16);
    PlanExchange exchange;
    CHECK (exchange.acquire() == nullptr);

    auto a = census.make(), b = census.make(), c = census.make();
    exchange.publish (a);
    exchange.publish (b);
    CHECK (exchange.acquire() == b.get());
    CHECK (exchange.acquire() == nullptr);

    exchange.publish (c);
    CHECK (exchange.acquire() == c.get());
    CHECK (exchange.acquire() == nullptr);
}

FREQART_TEST (exchangeGarbageRules)
{
    Census census (64);

    {
        PlanExchange exchange;

        // Before the audio thread has reported anything, every plan it may have is kept.
        exchange.publish (census.make());
        const auto* a = exchange.acquire();
        exchange.publish (census.make());
        const auto* b = exchange.acquire();
        exchange.publish (census.make());
        exchange.collectGarbage();
        CHECK (exchange.numAlive() == 3);

        // In use: b, fading from a. c is the latest. Nothing goes.
        exchange.reportInUse (b->version, a->version);
        exchange.collectGarbage();
        CHECK (exchange.numAlive() == 3);
        CHECK (! census.isDeleted (a->version));

        // The crossfade ended: a is older than both reported versions and goes.
        const auto aVersion = a->version;
        exchange.reportInUse (b->version, 0);
        exchange.collectGarbage();
        CHECK (exchange.numAlive() == 2);
        CHECK (census.isDeleted (aVersion));
        CHECK (! census.isDeleted (b->version));

        // c was never picked up and is replaced by d: the audio thread can never see c.
        const auto cVersion = exchange.latest()->version;
        exchange.publish (census.make());
        exchange.collectGarbage();
        CHECK (census.isDeleted (cVersion));
        CHECK (! census.isDeleted (b->version));
        CHECK (exchange.numAlive() == 2);

        // d picked up and in use alone: b goes, d stays.
        const auto bVersion = b->version;
        const auto* d = exchange.acquire();
        exchange.reportInUse (d->version, 0);
        exchange.collectGarbage();
        CHECK (census.isDeleted (bVersion));
        CHECK (exchange.numAlive() == 1);
        CHECK (exchange.latest().get() == d);

        // The latest plan is never released, whatever is reported.
        exchange.reportInUse (0, 0);
        exchange.collectGarbage();
        CHECK (exchange.numAlive() == 1);
        CHECK (! census.isDeleted (d->version));

        // A plan published after the last report is newer than anything reported: kept, even
        // once it is no longer the latest, until the audio thread moves past it.
        exchange.publish (census.make());
        const auto* e = exchange.acquire();
        exchange.publish (census.make());
        exchange.collectGarbage();
        CHECK (! census.isDeleted (e->version));
        CHECK (! census.isDeleted (d->version));
    }

    CHECK (census.alive.load() == 0);

    // While the audio thread is not running, superseded plans do not pile up.
    {
        PlanExchange idle;

        for (int i = 0; i < 20; ++i)
        {
            idle.publish (census.make());
            idle.collectGarbage();
        }

        CHECK (idle.numAlive() == 1);
        CHECK (census.alive.load() == 1);
    }

    CHECK (census.alive.load() == 0);
}

FREQART_TEST (exchangeStressTwoThreads)
{
    // A real message thread publishing (and collecting) as fast as it can while this thread
    // plays the audio thread: it picks plans up, crossfades over a few "blocks" like the
    // engine (queueing a plan that arrives during a crossfade), reports, and checks every plan
    // it holds is still alive and intact.
    constexpr int numPlans = 40000;
    Census census (numPlans + 1);
    std::atomic<bool> publisherDone { false };
    std::atomic<int> published { 0 };
    std::atomic<size_t> mostAlive { 0 };
    int acquired = 0, violations = 0;

    {
        PlanExchange exchange;

        std::thread publisher ([&]
        {
            for (int i = 1; i <= numPlans; ++i)
            {
                exchange.publish (census.make (i));   // versions are 1, 2, 3, ... so tag == version
                exchange.collectGarbage();
                published.store (i);
                mostAlive.store (std::max (mostAlive.load(), exchange.numAlive()));

                if (i % 64 == 0)
                    std::this_thread::yield();
            }

            publisherDone.store (true);
        });

        const ArtPlan* current = nullptr;
        const ArtPlan* fading = nullptr;
        const ArtPlan* queued = nullptr;
        uint64_t currentVersion = 0, fadingVersion = 0, queuedVersion = 0;
        int fadeLeft = 0;
        int fadeStartedAt = 0;

        // A crossfade lasts until the publisher has collected garbage twice more (or has
        // finished), so collection really does run while two plans are in use.
        const auto fadeIsOver = [&] { return publisherDone.load() || published.load() >= fadeStartedAt + 2; };

        const auto check = [&] (const ArtPlan* plan, uint64_t version)
        {
            if (plan == nullptr)
                return;

            if (census.isDeleted (version) || plan->numColumns != static_cast<int> (version))
                ++violations;
        };

        for (int spin = 0; ! publisherDone.load() || fadeLeft > 0 || queued != nullptr; ++spin)
        {
            if (const auto* plan = exchange.acquire())
            {
                ++acquired;

                if (plan->numColumns != static_cast<int> (plan->version))
                    ++violations;

                if (fadeLeft > 0)
                {
                    queued = plan;
                    queuedVersion = plan->version;
                }
                else
                {
                    fading = current;
                    fadingVersion = currentVersion;
                    current = plan;
                    currentVersion = plan->version;
                    fadeLeft = 1;
                    fadeStartedAt = published.load();
                }
            }

            if (fadeLeft > 0 && fadeIsOver())
            {
                fadeLeft = 0;
                fading = nullptr;
                fadingVersion = 0;

                if (queued != nullptr)
                {
                    fading = current;
                    fadingVersion = currentVersion;
                    current = queued;
                    currentVersion = queuedVersion;
                    queued = nullptr;
                    fadeLeft = 1;
                    fadeStartedAt = published.load();
                }
            }

            exchange.reportInUse (currentVersion, fadingVersion);

            check (current, currentVersion);
            check (fading, fadingVersion);
            check (queued, queuedVersion);

            if (spin % 256 == 0)
                std::this_thread::yield();
        }

        publisher.join();

        // The audio thread has the last plan; once collected, only it is left.
        if (const auto* last = exchange.acquire())
            exchange.reportInUse (last->version, 0);
        else
            exchange.reportInUse (currentVersion, 0);

        exchange.collectGarbage();
        CHECK (exchange.numAlive() == 1);
        CHECK (exchange.latest()->version == static_cast<uint64_t> (numPlans));
    }

    std::printf ("    %d plans published, %d picked up, at most %zu kept alive at once\n",
                 numPlans, acquired, mostAlive.load());

    CHECK (violations == 0);
    CHECK_GREATER (acquired, 10);
    CHECK_LESS (static_cast<double> (mostAlive.load()), 64.0);
    CHECK (census.alive.load() == 0);   // nothing leaked
}
