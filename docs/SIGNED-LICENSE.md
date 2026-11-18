# Signed licence: what the Worker sends, what the plug-ins check

This is the contract as deployed: Nova's version, Nov 18 2026, read from the Worker's code.

## Activate reply (200)

```json
{
  "ok": true,
  "activationToken": "<64 hex>",
  "license": { "payload": "anymeans-license-v1|<KEY>|<machineId>|<productId>", "signature": "<base64>", "alg": "Ed25519" },
  "activationsUsed": 1, "maxActivations": 1, "alreadyActive": false
}
```

- `KEY` is 20 characters: uppercase, no dashes.
- `machineId` is exactly what the plug-in sent.
- `productId` is the product the licence record belongs to, so for an ArtFreq Pro key it is the Pro product.
- `signature` is standard base64 of the 64-byte Ed25519 signature over the UTF-8 bytes of `payload`.
- `license` is `null` when the Worker has no signing key. The plug-in then does not activate.

## Public key (built into the plug-ins)

`7acdbd078095b8d36a9c91dfaa2166c820fbb382bc71d8659bfd1153231e2307`: raw 32 bytes, hex.

The private key stays a Worker secret (`LICENSE_SIGNING_KEY`). Keep a backup offline: if it is lost, shipped plug-ins cannot verify new activations until they are updated.

## What the plug-in checks, offline, on every launch

1. It rebuilds the payload from the stored key, this computer's hashed machine ID and the stored product ID.
2. The product must be one the plug-in accepts:
   - ArtFreq accepts ArtFreq and ArtFreq Pro.
   - ArtFreq FX accepts ArtFreq Pro only.
   - Crème and Vocalyst each accept their own product.
3. The signature must verify against the public key above.

If any check fails, the plug-in is not activated. Deactivating deletes the key, token, payload and signature.

## Test key

**AF8JB-EA2A0-X5ZPJ-N6W6C** (ArtFreq Pro). It needs this KV entry in `LICENSES`, under the name `AF8JBEA2A0X5ZPJN6W6C`:

```json
{"key":"AF8JBEA2A0X5ZPJN6W6C","productId":"prod_Ve87k69rq5fkZN","buyerEmail":"support@anymeansaudio.com","sessionId":null,"status":"unused","activations":[],"maxActivations":2,"createdAt":"2026-11-18T00:00:00.000Z"}
```

Delete it when testing is done, or keep it as your own copy.

## Still open on the Worker

- `maxActivations` is 1 for every product; the agreed number is 2.
- "ArtFreq Pro" has no download URL of its own in the purchase email.
- The email says "activates on one machine".
