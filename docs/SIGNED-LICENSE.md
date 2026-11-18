# Signed licence: what the Worker sends, what the plug-ins check

Deployed by Nova on the Worker, Nov 18 2026, and checked against its code. The plug-ins verify it offline.

## 1. Signing key

- Algorithm: **Ed25519** (RFC 8032, pure Ed25519 with SHA-512). In Workers: `crypto.subtle` with `{ name: "Ed25519" }`.
- Keep the private key as a Worker secret, `LICENSE_SIGNING_KEY` (PKCS#8, base64). Never put it in code or logs, and keep a backup offline. If it is lost, shipped plug-ins cannot verify new activations until they are updated.
- Public key, built into the plug-ins (raw 32 bytes, hex): `7acdbd078095b8d36a9c91dfaa2166c820fbb382bc71d8659bfd1153231e2307`.

## 2. Activate reply

Every successful `POST /api/activate` reply (including `alreadyActive`) adds two fields; everything else stays as it is:

```json
{
  "ok": true,
  "activationToken": "...",
  "license": "AMLIC1|prod_Ve87k69rq5fkZN|AF8JBEA2A0X5ZPJN6W6C|<machineId>|2026-11-18T12:00:00.000Z",
  "signature": "<base64>"
}
```

- `license` is the exact string that was signed, with fields joined by `|`:
  1. `AMLIC1`: the format version.
  2. The license record's `productId` (the product the key was bought for).
  3. The key, normalised: 20 characters, uppercase, no dashes.
  4. The `machineId` exactly as received, trimmed.
  5. The time of signing, ISO 8601 UTC.
- `signature`: standard base64, with padding, of the 64-byte Ed25519 signature over the UTF-8 bytes of `license`.
- Error replies carry no `license` or `signature`. Neither does a success reply when the Worker has no signing key, and the plug-in then does not activate.

## 3. What the plug-in checks

A plug-in counts as activated only when all of these hold:

1. The signature verifies against the built-in public key.
2. The version is `AMLIC1`.
3. The product is one this plug-in accepts:
   - ArtFreq accepts ArtFreq and ArtFreq Pro.
   - ArtFreq FX accepts ArtFreq Pro only.
   - Crème and Vocalyst each accept their own product.
4. The key matches the stored key.
5. The machine ID matches this computer's hashed ID.

It stores `license` and `signature` and re-checks them on every launch, offline. Copying the file to another computer fails check 5; editing it fails check 1.

## 4. Test key

**AF8JB-EA2A0-X5ZPJ-N6W6C** (ArtFreq Pro). It is live in KV. Delete it when testing is done, or keep it as your own copy.

## 5. Worker status

- Done: 2 machines per key, and the email says so.
- Still open: "ArtFreq Pro" has no download URL of its own in the purchase email, so it gets `DOWNLOAD_URL_DEFAULT`.
