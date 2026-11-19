#pragma once

// Waiting for the editor and the background jobs to catch up, on a machine of any speed.
//
// The editor follows the processor on timers (30 Hz) and change messages, the previews are made
// and the bounces and licence requests run on threads of their own, and their results come back
// through the message loop. On a fast machine a fixed pump (say 120 ms) is long enough for all of
// that; on a slow or busy one (the macOS runner's Intel half, run under Rosetta) the timers and
// the message loop run much later, and a check made after a fixed pump fails at random. So a
// check of what the editor or a job does after a change waits for it instead: the message loop
// runs in small steps until the condition holds or a generous timeout passes, and then the
// condition is checked.
//
//   setParameter (*effect, params::id::mode, 3.0f);
//   CHECK_EVENTUALLY (isShown (*editor, "sound-strip"));
//
// A check that something does NOT happen cannot wait for it (it holds at once, before the editor
// has even looked). Those wait for a positive sign first, something the same change does do, or
// for a fixed time, as before.
//
// Used by the tests and by the snapshot tool (tools/Snapshot.cpp), whose checks CI runs too.

#include "TestFramework.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <utility>

namespace testwait
{

/** How long a check waits for the editor or a job before it fails: generous, as a failure is the
    only case that waits this long. */
constexpr int timeoutMs = 10000;

/** Runs the message loop until `condition` holds or `timeout` ms have passed, asking every
    millisecond: after every message or few (a timer's tick, a change message, a result posted by
    a thread), so a state that lasts from one tick of a timer to the next is seen even when the
    machine stalls between them. Returns whether it holds (asked once more at the end, so a slow
    last step still counts). */
template <typename Condition>
bool pumpUntil (Condition&& condition, int timeout = timeoutMs)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds (timeout);

    while (! condition())
    {
        if (std::chrono::steady_clock::now() >= end)
            return condition();

        // (1 ms, not 0: on macOS a loop run for 0 ms dispatches nothing at all.)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }

    return true;
}

/** Runs the message loop until everything already posted to it has been dealt with: change
    messages, calls posted with callAsync, a text box's report of what was typed. The queue is
    first in, first out, so a call posted now runs after all of them. (Timers are not in the queue
    until they are due: wait for what they do with pumpUntil.) */
inline bool pumpPosted (int timeout = timeoutMs)
{
    const auto done = std::make_shared<std::atomic<bool>> (false);
    juce::MessageManager::callAsync ([done] { done->store (true); });
    return pumpUntil ([&done] { return done->load(); }, timeout);
}

/** Calls `look` on every round of the message loop's timers, ahead of the other timers in it (a
    1 ms timer is always the most overdue), so it runs between any two ticks of an editor's timer
    and sees every state the editor leaves from one tick to the next. pumpUntil cannot promise that
    on a stalled machine: on Linux the message loop deals with everything queued in one go, two
    ticks of a timer included. */
class TimerWatch final : private juce::Timer
{
public:
    explicit TimerWatch (std::function<void()> lookToUse) : look (std::move (lookToUse)) { startTimer (1); }
    ~TimerWatch() override { stopTimer(); }

private:
    void timerCallback() override { look(); }

    std::function<void()> look;
};

} // namespace testwait

/** CHECK (condition), once the message loop has run until it holds (or for testwait::timeoutMs). */
#define CHECK_EVENTUALLY(condition)                                                                     \
    test::check (testwait::pumpUntil ([&] { return static_cast<bool> (condition); }), __FILE__, __LINE__, \
                 "never true (waited 10 s): " #condition)
