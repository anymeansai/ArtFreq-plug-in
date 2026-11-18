# AnyMeans licensing

Key activation for the AnyMeans plug-ins, against the AnyMeans licence server (a Cloudflare Worker).
Self-contained: it needs only JUCE (`juce_core`, `juce_events`, `juce_cryptography`) and the vendored
Monocypher in `third_party/`. The same folder goes into ArtFreq, Crème and Vocalyst.

## How it works

1. The user types the key from the purchase email (`XXXXX-XXXXX-XXXXX-XXXXX`; any case, spaces or no
   dashes are fine). Its checksum is checked on the spot, so a typo is caught without the network.
2. The plug-in POSTs `{"key", "machineId", "productId"}` to `SERVER_BASE_URL/api/activate`, on a
   background thread. `machineId` is SHA-256 of `"AnyMeans|"` + the computer's hardware ID; the
   hardware ID itself never leaves the computer.
3. On success the server answers with a licence, `AMLIC1|<productId>|<KEY>|<machineId>|<time>`, and
   its Ed25519 signature. The plug-in checks the signature with the public key built into it, and that
   the licence is for a product it accepts, for that key and for this computer, before it stores
   anything.
4. The activation goes into one file shared by every AnyMeans plug-in of the user. From then on the
   plug-in checks the stored licence at every start (and whenever the file changes) and never needs
   the network again.
5. Deactivating POSTs `{"key", "machineId"}` to `/api/deactivate` and removes the activation here
   once the server confirms. Until it does, the activation stays: otherwise the key would stay taken
   on the server with nothing left to free it.

A file written by hand, or copied from another computer, unlocks nothing: an activation counts only
with a licence signed by the server's private key, for this computer.

## Files

| File | What it does |
| --- | --- |
| `LicenseConfig.h` | Every constant: server, public key, product IDs, support address, timeout, and what each plug-in of the repository accepts. **The only file to change when porting.** |
| `LicenseManager.*` | One per plug-in instance: `isActivated()` (lock-free, for the audio thread), `activate()`, `deactivate()`, status text, listeners. |
| `LicenseKey.*` | Key format: normalise, checksum, format, mask (`8K3QX-•••••-•••••-NV6ZH`). |
| `SignedLicense.*` | The licence string and its check. |
| `Crypto.*`, `Monocypher.*` | SHA-512, strict base64 and hex, Ed25519 verification (Monocypher's own pieces, as its optional `monocypher-ed25519.c` does). |
| `MachineId.*` | The hashed machine ID. |
| `LicenseStore.*` | The shared file, and the lock its writers take. |
| `SafeJson.h` | `juce::JSON::parse` for text from outside, refused when nested deep enough to overflow the parser's stack. |
| `LicenseTransport.*` | HTTPS through `juce::WebInputStream`, behind an interface tests replace with a fake server. |
| `third_party/monocypher/` | Monocypher 4.0.3, unmodified (BSD-2-Clause or CC0, see `LICENCE.md`). |

## The shared file

macOS `~/Library/Application Support/AnyMeans/Licenses.json`, Windows `%APPDATA%\AnyMeans\Licenses.json`,
Linux `~/.config/AnyMeans/Licenses.json`:

```json
{
  "format": 1,
  "activations": [
    {
      "productId": "prod_Ve87k69rq5fkZN",
      "product": "ArtFreq Pro",
      "key": "8K3QX7M2PDR9T4WNV6ZH",
      "machineId": "<64 hex>",
      "token": "<the server's activation token>",
      "license": "AMLIC1|prod_Ve87k69rq5fkZN|8K3QX7M2PDR9T4WNV6ZH|<64 hex>|2026-11-18T09:15:02.417Z",
      "signature": "<base64>",
      "activatedAt": "2026-11-18T10:15:03.000+01:00",
      "activatedIn": "ArtFreq FX"
    }
  ]
}
```

At most one activation per product, key and computer (a second key for the same product gets its
own entry, so its place on the server can still be freed). Activations for other computers (a
migrated or roaming user folder) are kept but not used. Readers skip what they don't understand, writers keep it.
Writes replace the file whole (temporary file, then swap) under a lock on the folder
(`LicenseStore::FolderLock`: `flock()` on a hidden `.Licenses.lock` on macOS and Linux, a named mutex on
Windows), which also keeps out the other AnyMeans plug-ins loaded into the same host process; JUCE's
`InterProcessLock` can't, on macOS and Linux, as its locks belong to the whole process. A damaged file
never crashes anything: it holds no activations, and the next activation replaces it (a copy is kept
as `Licenses.unreadable.json`).

## Porting to another plug-in

1. Copy this folder; compile its `.cpp` files (not `third_party/monocypher/monocypher.c`, which
   `Monocypher.cpp` includes) and link `juce::juce_cryptography`.
2. In `LicenseConfig.h`, replace the "plug-ins of this repository" section with that plug-in's own
   `PluginLicensing`; for Crème:
   `{ juce::String (juce::CharPointer_UTF8 ("Cr\xc3\xa8me")), { PRODUCT_ID_CREME }, {}, {}, {} }`.
   Spell anything outside ASCII as UTF-8 escapes like this: a plain `"Crème"` literal depends on the
   compiler's source encoding (MSVC without `/utf-8` reads the system code page), and `juce::String`
   asserts on 8-bit text. Leave the server, the public key and the products alone: they are shared.
3. Make one `LicenseManager` per plug-in instance (in the processor), read `isActivated()` on the
   audio thread, and show `getStatusText()`, `activate()` and `deactivate()` in the editor. Never call
   the server while a host scans or renders; call `cancelRequest()` when the editor closes, if it
   should stop a request.

## Tests

`tests/LicenseTests.cpp`, with `tests/LicenseTestSupport.h` (the Worker's real answers as vectors, a
fake server and a test environment), and `tests/LicenseVectorTests.cpp`, which checks the same code
against a second, independent set, `tests/data/license-vectors.json`: the Worker's own code run with a
throwaway key of its own, WebCrypto's verdicts on a few hundred Ed25519 signatures and their
alterations, Node's SHA-512 and the server's checksum verdicts on typed keys. That file was made by
`tests/data/make-license-vectors.mjs` (Node 22, with a copy of the Worker's sources next to it). Tests and tools never reach the server: a `LicenseManager` takes
an `Environment` (folder, transport, machine ID, public key), and
`LicenseManager::setDefaultEnvironmentForTesting()` sets one for code that makes plug-in instances the
ordinary way. Only code can do either; no file, setting or switch changes a shipping plug-in.
