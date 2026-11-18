#include "LicenseTransport.h"

namespace anymeans::license
{

void CancelToken::cancel()
{
    const juce::ScopedLock sl (lock);
    cancelled = true;

    // Run under the lock, so the transport can't destroy what the action refers to meanwhile
    // (clearAbortAction waits for it).
    if (abortAction != nullptr)
        abortAction();
}

bool CancelToken::setAbortAction (std::function<void()> action)
{
    const juce::ScopedLock sl (lock);

    if (cancelled)
        return false;

    abortAction = std::move (action);
    return true;
}

void CancelToken::clearAbortAction()
{
    const juce::ScopedLock sl (lock);
    abortAction = nullptr;
}

//==============================================================================
namespace
{

// The server's answers are a few hundred bytes; anything far bigger is not one of them (a
// captive portal's page, say), and is cut short rather than read whole.
constexpr int maxReplyBytes = 64 * 1024;

class HttpsTransport final : public Transport
{
public:
    explicit HttpsTransport (juce::String base) : baseUrl (std::move (base)) {}

    HttpReply post (const juce::String& path, const juce::String& jsonBody, int timeoutMs, CancelToken& token) override
    {
        HttpReply reply;

        const auto url = juce::URL (baseUrl + path).withPOSTData (jsonBody);
        juce::WebInputStream stream (url, true);
        stream.withExtraHeaders ("Content-Type: application/json\r\nAccept: application/json")
              .withConnectionTimeout (timeoutMs);

        // WebInputStream::cancel() may be called from another thread: it aborts connect() and read().
        if (! token.setAbortAction ([&stream] { stream.cancel(); }))
        {
            reply.outcome = HttpReply::Outcome::cancelled;
            return reply;
        }

        if (stream.connect (nullptr) && ! stream.isError() && stream.getStatusCode() > 0)
        {
            juce::MemoryOutputStream received;
            char buffer[4096];

            while (! stream.isExhausted() && received.getDataSize() < static_cast<size_t> (maxReplyBytes))
            {
                const auto numRead = stream.read (buffer, static_cast<int> (sizeof (buffer)));

                if (numRead <= 0)
                    break;

                received.write (buffer, static_cast<size_t> (numRead));
            }

            reply.outcome = HttpReply::Outcome::answered;
            reply.statusCode = stream.getStatusCode();
            reply.body = juce::String::fromUTF8 (static_cast<const char*> (received.getData()),
                                                 static_cast<int> (received.getDataSize()));
        }

        token.clearAbortAction();

        if (token.isCancelled())
            reply.outcome = HttpReply::Outcome::cancelled;

        return reply;
    }

private:
    const juce::String baseUrl;
};

} // namespace

std::shared_ptr<Transport> makeHttpsTransport (const juce::String& baseUrl)
{
    return std::make_shared<HttpsTransport> (baseUrl);
}

} // namespace anymeans::license
