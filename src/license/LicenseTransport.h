#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>

namespace anymeans::license
{

/** Lets one thread stop a request another thread is waiting on. The transport registers how to
    interrupt its wait; cancel() runs that, from any thread, and every later wait returns at once.
*/
class CancelToken
{
public:
    CancelToken() = default;

    /** Stops the request: interrupts the wait in progress, if any, and every later one. */
    void cancel();

    bool isCancelled() const noexcept   { return cancelled.load(); }

    /** For transports: what cancel() must do to interrupt the wait about to start. Returns false
        (and keeps nothing) if the token is already cancelled. Clear it before whatever it refers to
        goes away: cancel() runs it under the same lock, so after clearAbortAction() returns it is
        no longer running and won't run again.
    */
    bool setAbortAction (std::function<void()> action);
    void clearAbortAction();

private:
    juce::CriticalSection lock;
    std::function<void()> abortAction;
    std::atomic<bool> cancelled { false };

    JUCE_DECLARE_NON_COPYABLE (CancelToken)
};

/** What came back from one POST. */
struct HttpReply
{
    enum class Outcome
    {
        answered,      // the server answered: statusCode and body are what it sent
        noConnection,  // no answer: offline, DNS, TLS, refused, or the transport's own timeout
        cancelled      // stopped through the CancelToken
    };

    Outcome outcome = Outcome::noConnection;
    int statusCode = 0;
    juce::String body;
};

/** How the licence code talks to the server. The plug-in uses HTTPS (makeHttpsTransport); tests use
    a fake server, so they never need the network.
*/
class Transport
{
public:
    virtual ~Transport() = default;

    /** POSTs a JSON body to the server's path ("/api/activate") and waits for the whole answer, at
        most about timeoutMs, or until the token is cancelled. Called on a background thread, never
        the message or audio thread; may be called from several threads at once.
    */
    virtual HttpReply post (const juce::String& path, const juce::String& jsonBody, int timeoutMs, CancelToken& token) = 0;
};

/** The real thing: HTTPS to baseUrl through juce::WebInputStream (NSURLSession on macOS, WinINet
    on Windows), with Content-Type: application/json.
*/
std::shared_ptr<Transport> makeHttpsTransport (const juce::String& baseUrl);

} // namespace anymeans::license
