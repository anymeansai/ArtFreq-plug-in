# Backlog: next round of changes

Collected with the customer; to be done together in one release.

## 1. Update check

- The website hosts `version.json`, e.g. `{"latest": "0.5.0", "url": "https://.../download"}` (URL: one constant).
- When the editor opens, at most once a day, a background thread fetches it (HTTPS, about 10 s timeout, never on the
  audio thread, never while the host only scans or renders). If it is newer than the running version, the header shows a
  small "Update available" badge that opens the URL in the browser.
- A "Check for updates" switch in Settings (on by default). Failures are silent. Note it in the privacy policy.

## 2. License activation

**Decisions (customer):** our own server, on Cloudflare (free: Workers + D1; the website anymeansaudio.com moves there from Vercel too, keeping the support@ email's MX records); 2 activations (computers) per key; the plug-ins must run offline
after activation; the SAME licensing code, server and flow in all three AnyMeans plug-ins (ArtFreq + ArtFreq FX, Crème,
Vocalyst; each product its own product ID; one shared AnyMeans app-data folder), so Crème and Vocalyst get changed too.

**Selling (customer):** ArtFreq FX is never sold by itself: it only comes bundled with ArtFreq. One purchase, one key,
both plug-ins; no separate FX product ID in the store.

**Consequences / still to settle:**
- Vercel's free Hobby plan is for non-commercial personal use only; selling needs Pro (paid). The free alternative is
  Cloudflare Workers, whose free plan allows commercial use (to double-check at the time).
- Lemon Squeezy is being steered toward Stripe Managed Payments, which has no license keys. So the server issues keys
  itself: the store's "order paid" webhook goes to the server, which makes the key, stores it and emails it. Changing store
  later then breaks nothing.
- A database for keys and activations (a free Postgres/Redis tier on the host).
- The server's address must be our own domain (e.g. api.anymeansaudio.com), never *.vercel.app, because it is built
  into shipped plug-ins.
- The server signs each activation (Ed25519 over key, machine ID and product). The plug-ins check it with a built-in public
  key, so activation works offline and cannot be faked by editing a file. Keep the private key secret, in the host's
  environment variables, and backed up.
- Offline activation for computers that are never online: the plug-in shows a request code, the user enters it on the
  website from another device and pastes the reply back.
- Lost or dead computer: a "free my activations" page on the website (key + purchase email) or support.
- Refunds and chargebacks cannot be revoked on machines that stay offline. Accepted.
- Which products a key unlocks (ArtFreq key = ArtFreq + ArtFreq FX), and whether there are bundles.
- What 0.x testers get (free keys?).
- A privacy policy and EULA.

**Review of the pasted spec:**

The spec the customer pasted (from Meta's Muse AI) has a sound outline is sound (hashed machine ID, works offline after
activation, always lets the user retry, a Deactivate button, server URL and product ID as constants). Before building,
fix these:

- **Server.** (Superseded: we now use our own server, see above.) It assumes `/api/activate` and `/api/deactivate`.
  We picked Lemon Squeezy: use its License API instead (`POST https://api.lemonsqueezy.com/v1/licenses/activate` with
  `license_key` + `instance_name`, returns an `instance_id`; `/deactivate` takes `license_key` + `instance_id`;
  `/validate`). Its responses are not `{"ok", "activationToken"}` and its error cases are not the spec's 409/403, so map
  them. Its keys are UUID-like, so do not hard-code `XXXXX-XXXXX-XXXXX-XXXXX`. Normalise what is typed (trim, case,
  dashes) and check the format before sending.
- **Faked activation.** If the plug-in only checks "is a token stored?", anyone can write one into the settings file. With
  our own server, the token should be signed (Ed25519 over key + machine ID + product) and checked against a public key
  built into the plug-in. With Lemon Squeezy, store the instance ID and key and accept light protection (as agreed).
- **One purchase, two plug-ins.** ArtFreq and ArtFreq FX must share one activation, stored in a shared AnyMeans
  app-data folder, not one per plug-in.
- **No separate window.** In a plug-in, show the key prompt as an overlay inside the editor. Never at construction, while
  the host scans, or during offline renders and pluginval. Network calls run on a background thread.
- **Unactivated behaviour is not specified.** Decide with the customer what the plug-in does before activation (full
  features with a reminder, periodic silence, or a locked editor). Projects must still open and render.
- **Same computer again.** Re-activating the same machine (after a reinstall, or from another macOS user account) must
  succeed and not use up a second seat. Reuse the stored instance, or have the server treat the same machine ID as the
  same seat.
- **Machine ID.** On macOS use the hardware UUID (IOPlatformUUID). Hash it with SHA-256 plus an AnyMeans/ArtFreq salt, so
  the hash cannot be matched across other vendors' software.
- **Errors the spec misses.** 400/404/429/5xx, timeouts, and 200 with a failure inside: show "Something went wrong, try
  again" plus the server's message, and keep Retry available.
- **Deactivate while offline.** The spec deletes the local token after the request, even if the request fails. Then the
  seat stays used on the server and the user is stuck. Delete only after the server confirms; otherwise show the error and
  the support address.
- **Lost or dead computer.** The user cannot deactivate it from the plug-in. Support (or the store's customer page) must
  be able to free the seat.
- **Seat limit.** Set it at the store. One machine is harsh for producers with a studio and a laptop; 2-3 is usual.
- **Support address.** Confirm that `support@anymeansaudio.com` exists.
- **CI and tests.** pluginval, the tests and the snapshot tool must run without activation and without network: add a
  build flag or test hook, and never call the real server from CI.
- **Privacy.** Mention the hashed machine ID in the privacy policy.
