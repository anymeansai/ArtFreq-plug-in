// Makes tests/data/license-vectors.json, read by tests/LicenseVectorTests.cpp. Run once, by hand:
//
//   copy the deployed Worker's worker.js, license.js and license-store-kv.js next to this file as
//   worker.mjs, license.mjs and license-store-kv.mjs (with worker.mjs importing the .mjs names), then
//   node make-license-vectors.mjs license-vectors.json      (Node 22: WebCrypto has Ed25519)
//
// The Ed25519 keys are made fresh on every run and their private halves are never written anywhere,
// so the file's licences only ever verify with the public key stored next to them. Every run gives
// different (equally valid) vectors; the tests read whatever the file holds.
//
import { webcrypto as wc, createHash, randomBytes } from "node:crypto";
import worker from "./worker.mjs";
import { isValidKeyFormat, generateLicenseKey, createLicense, normalizeKey } from "./license.mjs";
import { createKvStore } from "./license-store-kv.mjs";
import { writeFileSync } from "node:fs";

const subtle = wc.subtle;
const hex = (b) => Buffer.from(b).toString("hex");
const b64 = (b) => Buffer.from(b).toString("base64");

// ---------------------------------------------------------------- curve arithmetic (BigInt)
const p = 2n ** 255n - 19n;
const L = 2n ** 252n + 27742317777372353535851937790883648493n;
const mod = (a, m = p) => ((a % m) + m) % m;
const pow = (b, e, m = p) => { let r = 1n; b = mod(b, m); while (e > 0n) { if (e & 1n) r = r * b % m; b = b * b % m; e >>= 1n; } return r; };
const inv = (a) => pow(a, p - 2n);
const d = mod(-121665n * inv(121666n));
const I = pow(2n, (p - 1n) / 4n);
function decode(bytes) {
  let y = 0n; for (let i = 31; i >= 0; i--) y = (y << 8n) | BigInt(bytes[i]);
  const sign = y >> 255n; y &= (1n << 255n) - 1n;
  if (y >= p) return { ok: false, why: "y >= p" };
  const u = mod(y * y - 1n), v = mod(d * y * y + 1n);
  let x = mod(u * pow(v, 3n) * pow(u * pow(v, 7n), (p - 5n) / 8n));
  if (mod(v * x * x) !== u) { if (mod(v * x * x) === mod(-u)) x = mod(x * I); else return { ok: false, why: "not on curve" }; }
  if ((x & 1n) !== sign) x = mod(-x);
  return { ok: true, P: [x, y, 1n, mod(x * y)] };
}
function add(P, Q) {
  const [X1, Y1, Z1, T1] = P, [X2, Y2, Z2, T2] = Q;
  const A = mod((Y1 - X1) * (Y2 - X2)), B = mod((Y1 + X1) * (Y2 + X2)), C = mod(2n * d * T1 * T2), D = mod(2n * Z1 * Z2);
  const E = B - A, F = D - C, G = D + C, H = B + A;
  return [mod(E * F), mod(G * H), mod(F * G), mod(E * H)];
}
function mul(n, P) { let R = [0n, 1n, 1n, 0n]; let Q = P; while (n > 0n) { if (n & 1n) R = add(R, Q); Q = add(Q, Q); n >>= 1n; } return R; }
const isIdentity = (P) => mod(P[0]) === 0n && mod(P[1] - P[2]) === 0n;

const PRODUCTION_KEY = "7acdbd078095b8d36a9c91dfaa2166c820fbb382bc71d8659bfd1153231e2307";
const prodPoint = decode(Buffer.from(PRODUCTION_KEY, "hex"));
const productionKeyCheck = {
  decodes: prodPoint.ok,
  smallOrder: prodPoint.ok ? isIdentity(mul(8n, prodPoint.P)) : null,
  primeOrder: prodPoint.ok ? isIdentity(mul(L, prodPoint.P)) : null,
};

// ---------------------------------------------------------------- keys
const liveTestKey = "AF8JB-EA2A0-X5ZPJ-N6W6C";
const keyChecks = { liveTestKeyValid: await isValidKeyFormat(liveTestKey), liveTestKeyNormalised: normalizeKey(liveTestKey) };

// ---------------------------------------------------------------- the Worker, with a throwaway key
const pair = await subtle.generateKey({ name: "Ed25519" }, true, ["sign", "verify"]);
const publicRaw = new Uint8Array(await subtle.exportKey("raw", pair.publicKey));
const pkcs8 = b64(new Uint8Array(await subtle.exportKey("pkcs8", pair.privateKey)));

const kvData = new Map();
const kv = {
  async get(k, type) { const v = kvData.get(k); return v === undefined ? null : (type === "json" ? JSON.parse(v) : v); },
  async put(k, v) { kvData.set(k, v); },
  async list() { return { keys: [...kvData.keys()].map((name) => ({ name })), list_complete: true }; },
};
const env = { LICENSES: kv, LICENSE_TOKEN_SECRET: "skeptic-test-secret", LICENSE_SIGNING_KEY: pkcs8 };
const envUnsigned = { LICENSES: kv, LICENSE_TOKEN_SECRET: "skeptic-test-secret" };
const store = createKvStore(kv);

const ART = "prod_Ve87TF5PUEzRgj", PRO = "prod_Ve87k69rq5fkZN", CREME = "prod_Ve87zT8mss1owN";
const artKey = (await createLicense(store, { productId: ART })).key;
const proKey = (await createLicense(store, { productId: PRO })).key;
const cremeKey = (await createLicense(store, { productId: CREME })).key;
const machine = (s) => createHash("sha256").update("AnyMeans|" + s).digest("hex");
const here = machine("skeptic here"), a1 = machine("skeptic a1"), a2 = machine("skeptic a2");

async function call(path, body, e = env) {
  const r = await worker.fetch(new Request("https://x.workers.dev" + path, { method: "POST", body: typeof body === "string" ? body : JSON.stringify(body) }), e);
  return { status: r.status, body: await r.text() };
}
const fmt = (k) => k.match(/.{5}/g).join("-");
const replies = {};
replies.artFresh = await call("/api/activate", { key: fmt(artKey), machineId: here, productId: ART });
replies.artAgain = await call("/api/activate", { key: fmt(artKey), machineId: here, productId: ART });
replies.artUnsigned = await call("/api/activate", { key: fmt(artKey), machineId: here, productId: ART }, envUnsigned);
replies.proAsArt = await call("/api/activate", { key: fmt(proKey), machineId: here, productId: ART });
replies.proFresh = await call("/api/activate", { key: fmt(proKey), machineId: here, productId: PRO });
replies.artAsPro = await call("/api/activate", { key: fmt(artKey), machineId: here, productId: PRO });
replies.cremeAsArt = await call("/api/activate", { key: fmt(cremeKey), machineId: here, productId: ART });
replies.cremeAsPro = await call("/api/activate", { key: fmt(cremeKey), machineId: here, productId: PRO });
// The limit: two other machines take the Pro key... it already has one (here); one more fills it.
replies.proOther1 = await call("/api/activate", { key: fmt(proKey), machineId: a1, productId: PRO });
replies.proLimit = await call("/api/activate", { key: fmt(proKey), machineId: a2, productId: PRO });
replies.deactivateArt = await call("/api/deactivate", { key: fmt(artKey), machineId: here });
replies.deactivateArtAgain = await call("/api/deactivate", { key: fmt(artKey), machineId: here });
// A machine ID with spaces round it: the server trims it, and the licence carries the trimmed one.
replies.paddedMachine = await call("/api/activate", { key: fmt(cremeKey), machineId: "  " + here + " ", productId: CREME });
// No productId at all: the server skips its product check (the plug-in always sends one).
replies.noProduct = await call("/api/activate", { key: fmt(cremeKey), machineId: a1 });

// ---------------------------------------------------------------- Ed25519 differential corpus
const vpair = await subtle.generateKey({ name: "Ed25519" }, true, ["sign", "verify"]);
const vpub = new Uint8Array(await subtle.exportKey("raw", vpair.publicKey));
async function nodeVerifies(pub, sig, msg) {
  try {
    const k = await subtle.importKey("raw", pub, { name: "Ed25519" }, false, ["verify"]);
    return await subtle.verify("Ed25519", k, sig, msg);
  } catch { return false; }
}
const le = (n) => { const b = new Uint8Array(32); for (let i = 0; i < 32; i++) { b[i] = Number(n & 0xffn); n >>= 8n; } return b; };
const fromLe = (b) => { let n = 0n; for (let i = b.length - 1; i >= 0; i--) n = (n << 8n) | BigInt(b[i]); return n; };

// The small-order points' encodings (as listed by libsodium's blocklist; the canonical ones).
const smallOrder = [
  "0100000000000000000000000000000000000000000000000000000000000000",
  "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
  "0000000000000000000000000000000000000000000000000000000000000000",
  "0000000000000000000000000000000000000000000000000000000000000080",
  "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
  "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc85",
  "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a",
  "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac03fa",
];

const cases = [];
function realisticLicence() {
  const prods = [ART, PRO, CREME, "prod_Ve87yi93nh2Rth"];
  const t = new Date(Date.UTC(2026, 10, 18) + Math.floor(Math.random() * 3e10)).toISOString();
  return { prod: prods[Math.floor(Math.random() * 4)], machineId: hex(randomBytes(32)), time: t };
}
for (let i = 0; i < 30; i++) {
  let msg;
  if (i < 16) {
    const r = realisticLicence();
    msg = new TextEncoder().encode(`AMLIC1|${r.prod}|${await generateLicenseKey()}|${r.machineId}|${r.time}`);
  } else {
    const n = [0, 1, 31, 32, 63, 64, 111, 112, 127, 128, 129, 255, 256, 600][i - 16];
    msg = new Uint8Array(randomBytes(n));
  }
  const sig = new Uint8Array(await subtle.sign("Ed25519", vpair.privateKey, msg));
  const add = async (kind, pub, s, m) => cases.push({ kind, pub: hex(pub), sig: hex(s), msg: hex(m), node: await nodeVerifies(pub, s, m) });
  await add("good", vpub, sig, msg);

  // One flipped bit in the message, the signature, the key.
  if (msg.length > 0) { const m = msg.slice(); m[Math.floor(Math.random() * m.length)] ^= 1 << Math.floor(Math.random() * 8); await add("message bit", vpub, sig, m); }
  { const s = sig.slice(); s[Math.floor(Math.random() * 32)] ^= 1 << Math.floor(Math.random() * 8); await add("R bit", vpub, s, msg); }
  { const s = sig.slice(); s[32 + Math.floor(Math.random() * 32)] ^= 1 << Math.floor(Math.random() * 8); await add("S bit", vpub, s, msg); }
  { const k = vpub.slice(); k[Math.floor(Math.random() * 32)] ^= 1 << Math.floor(Math.random() * 8); await add("key bit", k, sig, msg); }
  // One byte more or less.
  { const m = new Uint8Array(msg.length + 1); m.set(msg); m[msg.length] = i & 0xff; await add("message longer", vpub, sig, m); }
  if (msg.length > 0) await add("message shorter", vpub, sig, msg.slice(0, msg.length - 1));

  // Malleability: S + L and S + 2L (still under 2^256 when they fit), and the top bit set.
  const S = fromLe(sig.slice(32));
  for (const [kind, s2] of [["S+L", S + L], ["S+2L", S + 2n * L], ["S top bit", S | (1n << 255n)], ["S=L", L], ["S=0", 0n]]) {
    if (s2 >= 2n ** 256n) continue;
    const s = sig.slice(); s.set(le(s2), 32); await add(kind, vpub, s, msg);
  }
  // R and A replaced by small-order points.
  if (i % 10 === 0) {
    for (const so of smallOrder) {
      const s = sig.slice(); s.set(Buffer.from(so, "hex"), 0); await add("R small order", vpub, s, msg);
      await add("A small order", Buffer.from(so, "hex"), sig, msg);
    }
  }
}

// Small-order public key forgeries: A = identity, R = identity, S = 0 holds for any message
// with the cofactored equation. The plug-in's key is fixed (and checked above), so this only
// documents the verifier's behaviour.
for (const so of smallOrder) {
  const forged = new Uint8Array(64); forged.set(Buffer.from(smallOrder[0], "hex"), 0);
  const m = new TextEncoder().encode("AMLIC1|prod_Ve87k69rq5fkZN|AAAAAAAAAAAAAAAAAAAA|00|t");
  cases.push({ kind: "forgery with small-order key", pub: so, sig: hex(forged), msg: hex(m), node: await nodeVerifies(Buffer.from(so, "hex"), forged, m) });
}

// ---------------------------------------------------------------- key checksums, the server's verdicts
const ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
const keyCases = [];
for (let i = 0; i < 48; i++) {
  const k = await generateLicenseKey();
  keyCases.push({ key: k, server: await isValidKeyFormat(k) });
  const pos = Math.floor(Math.random() * 20);
  let c; do { c = ALPHABET[Math.floor(Math.random() * 32)]; } while (c === k[pos]);
  const sub = k.slice(0, pos) + c + k.slice(pos + 1);
  keyCases.push({ key: sub, server: await isValidKeyFormat(sub) });
  const t = Math.floor(Math.random() * 19);
  const swapped = k.slice(0, t) + k[t + 1] + k[t] + k.slice(t + 2);
  keyCases.push({ key: swapped, server: await isValidKeyFormat(swapped) });
}

// ---------------------------------------------------------------- SHA-512, random lengths
const sha = [];
for (let i = 0; i < 24; i++) {
  const n = i < 18 ? [0, 1, 110, 111, 112, 113, 127, 128, 129, 239, 240, 241, 255, 256, 257, 1023, 1024, 1025][i] : Math.floor(Math.random() * 600);
  const data = randomBytes(n);
  sha.push({ data: hex(data), digest: createHash("sha512").update(data).digest("hex") });
}

writeFileSync(process.argv[2], JSON.stringify({
  note: "Made once by a Node 22 script: the deployed Worker code (worker.js, license.js) with an in-memory KV and a throwaway Ed25519 key whose private half was not kept, plus WebCrypto Ed25519 and crypto SHA-512 verdicts. Read by tests/LicenseVectorTests.cpp.",
  productionKeyCheck, keyChecks,
  worker: { publicKey: hex(publicRaw), keys: { artFreq: artKey, pro: proKey, creme: cremeKey }, machineHere: here, replies },
  ed25519: cases, sha512: sha, keys: keyCases,
}, null, 1) + "\n");
console.log(JSON.stringify({ productionKeyCheck, keyChecks, cases: cases.length, divergent: cases.filter((c) => c.kind !== "good" && c.node).map((c) => c.kind) }));
console.log(replies.paddedMachine.body);
console.log(replies.noProduct.body);
