# Node Authentication: Security Design

This document explains the security model behind DAOS node
authentication. It is a companion to the [main design document][design]
which covers the feature interfaces, configuration, and code changes.

This document is intended for DAOS developers who need to understand
*why* the security mechanisms work the way they do, without requiring
deep cryptography expertise.

## Summary

Per-pool node authentication adds a **machine-level authorization gate**
in front of the existing user credential and ACL checks. A pool can be
configured with a pool-specific certificate authority (CA). Only client
nodes that possess a certificate signed by that CA can connect.

The key security properties:

- **Blast radius containment**: A compromised node can only access
  pools it has certificates for, not every pool on the system.
- **Per-pool isolation**: Each pool has its own CA, so a certificate
  for pool A is useless for pool B.
- **Defense in depth**: Node auth doesn't replace user auth — it adds
  a layer on top of it.
- **Cryptographic revocation**: Compromised or exfiltrated certs can
  be permanently invalidated via per-CN revocation watermarks,
  without CRL infrastructure and without the ambiguity of hostname-
  based denylists. A single `dmg pool revoke-client --evict-all-handles`
  invocation cryptographically kills the old cert, issues a
  replacement, and drops any live handles the attacker may hold.

## Trust Model

Five entities participate in per-pool node authentication. Each has a
specific role and trust boundary:

```
+-------------------------------------------------------------------+
|  Administrative Trust Domain (offline key management)              |
|                                                                   |
|   DAOS CA             Pool CA "foo"         Pool CA "bar"         |
|   (system root        (signs node certs     (signs node certs     |
|    of trust)           for pool foo)         for pool bar)        |
+-------------------------------------------------------------------+

+---------------------------+     +---------------------------+
|  Client Node              |     |  Server                   |
|                           |     |                           |
|  +--------+  +---------+  |     |  +--------+  +---------+  |
|  | Client |  |  Agent  |  |     |  | Engine |  | Control |  |
|  | Process|  | Daemon  |  |     |  |  (C)   |  | Plane   |  |
|  +--------+  +---------+  |     |  +--------+  | (Go)    |  |
|      |       holds node   |     |      |       +---------+  |
|      |       private keys |     |      |       does crypto  |
|      |       (per-pool)   |     |      |       validation   |
+---------------------------+     +---------------------------+
        |                                    |
        +-------- fabric (cleartext) --------+
```

### What each entity is trusted to do

| Entity | Trusted to | Trust boundary |
|--------|-----------|----------------|
| **DAOS CA** | Sign pool CAs. Root of the certificate chain. | Private key is high-value and should be carefully protected. Compromise allows creating pool CAs for any pool. |
| **Pool CA** | Sign node certificates for one specific pool. | Private key used during node provisioning, then stored securely. Compromise allows issuing node certs for that pool only. |
| **Agent** | Hold node/tenant private keys. Validate cert CN prefix as a defense-in-depth check against misdeployment (`node:` certs must match local hostname; `tenant:` certs skip hostname check). Sign PoP on behalf of client processes. Determine caller identity via domain socket. | Trust anchor on the client node. Compromised agent can forge PoP for any pool the node has certs for. Same boundary as existing credential signing. The CN check is not a security boundary against an attacker who controls the client machine — see [Private key exfiltration](#private-key-exfiltration). |
| **Client process** | Send an authenticated pool connect request. | Never sees private keys. Can only use what the agent gives it. |
| **Server control plane** | Validate certificate chains and PoP signatures. Enforce pool CA requirements. | Trusts the DAOS CA cert it was configured with. |

### Key insight: the agent trust boundary doesn't expand

Today, the agent is already the most trusted component on a client node
-- it signs user credentials. Per-pool certificates don't expand this
trust boundary. The agent gains access to node certificate private keys,
but these are lower-sensitivity than the credential-signing capability
it already has.

For pools with node auth enabled, the **blast radius** actually narrows:
a compromised agent can only reach node-auth pools for which the node
holds valid certs.

## Threat Analysis

### Threats this feature mitigates

#### 1. Root compromise on a client node

**The attack**: An attacker gains root on a client node. Using root
privileges, they call `setuid()`/`setgid()` to become any local user,
connect to the agent's domain socket, and obtain validly-signed
credentials for that user. Today, this gives the attacker access to
*every pool* that any user on the node is authorized for.

**With per-pool certs**: The agent will only produce a PoP signature for
pools where it has a node certificate. The attacker can impersonate any
user, but they can only reach pools for which the compromised node has
been issued a certificate. If the node has certs for pools A and B but
not C, pool C is inaccessible regardless of what user the attacker
impersonates.

**Residual risk**: Pools that the node *does* have certs for are still
exposed. The feature limits *which* pools are exposed, not *what* can be
done within them.

#### 2. Lateral movement across pools

**The attack**: After compromising a node with access to pool A, the
attacker attempts to access pool B.

**With per-pool certs on both pools**: Each pool has its own CA. The
node certificate for pool A was signed by pool A's CA. Pool B validates
against pool B's CA. The certificate for pool A fails chain validation
for pool B.

**Residual risk**: Node authentication is opt-in per pool. If pool B
does not have `DAOS_PROP_PO_POOL_CA` set, the cert check is skipped
entirely (see [Validation Chain](#validation-chain), step 2). The
attacker connects to pool B via credential/ACL checks alone — the same
posture as without this feature. Cross-pool isolation only exists
between pools that both enforce node auth.

### Threats this feature does NOT mitigate

#### Rogue client node joins the fabric

Already prevented by agent mTLS: a machine without agent key material
cannot establish the gRPC channel or sign valid AUTH_SYS credentials.
Node auth is not the defense here, though it does add a second gate for
pools that enforce it.

Note: the agent cert is shared across all agents in the system -- it
does not provide per-node identity or key isolation. See
[Future Directions](#future-directions).

#### Compromised agent daemon

If an attacker gains control of the agent process, they have all node
private keys and can forge PoP for any pool the node has certs for.

**Why acceptable**: The agent already signs credentials. A compromised
agent already means full access to everything the node can reach.

#### User impersonation on a node with a valid cert

Node auth gates *machine* access. Within an authenticated node, user
impersonation works exactly as today.

**Why acceptable**: User identity and node identity are orthogonal. Both
checks must pass: valid node cert AND valid user credential with
sufficient ACL permissions.

#### Private key exfiltration

A root attacker on a client node can copy a node's private key and
certificate to another machine. The exfiltrated cert+key remain
valid as far as the pool CA is concerned — standard X.509 chain
validation will accept them from any presenter.

The defense is **cryptographic revocation via per-CN watermarks**,
not the agent's CN check. On every pool connect for a pool with
node auth enabled, the server compares the presented cert's
`NotBefore` against the watermark recorded for its CN in
`DAOS_PROP_PO_CERT_WATERMARKS`. When the administrator runs
`dmg pool revoke-client --node <name>`, the control library
atomically issues a fresh replacement cert (whose `NotBefore` equals
the new watermark) and advances the pool's watermark for that CN.
From that instant forward, any cert with an earlier `NotBefore` —
including every previously-exfiltrated copy — is rejected with
`-DER_BAD_CERT`, regardless of which machine presents it, what
hostname that machine claims, or whether the PoP signature is
otherwise valid.

**The agent's CN check is not a security boundary against
exfiltration.** The check exists so that a legitimate node running
the stock agent refuses to load a misdeployed cert — it catches
configuration mistakes, not attackers. An attacker who controls
their own machine can trivially run a modified agent (or a
purpose-built client) that skips the CN check, sets the AUTH_SYS
credential's machine name to whatever matches the cert CN, and
signs a valid PoP with the exfiltrated private key. All of this
passes `processValidateNodeCert` up to the watermark check. The
watermark is the only server-side defense that cannot be
circumvented by the attacker-controlled client side.

Several consequences follow from this:

- **Scenario A: attacker keeps own hostname, runs stock agent.**
  The stock agent refuses to load the mismatched cert. This is
  the only attacker scenario the agent CN check actually defeats,
  and it only defeats *lazy* attackers.
- **Scenario B: attacker runs modified client.** CN check is
  bypassed; cert chains, PoP verifies, CN cross-validation passes
  (attacker sets machine name to match cert CN). The watermark
  check is the load-bearing defense. Before the admin runs
  `revoke-client`, the attacker has full access via the exfiltrated
  cert. After, the cert is dead.
- **Scenario C: attacker holds a live handle at revocation time.**
  Node cert validation only runs on initial `POOL_CONNECT`, so an
  existing handle survives a watermark advance by itself. The
  `--evict-all-handles` flag on `revoke-client` pairs revocation
  with a pool-wide `dmg pool evict`, forcing every active handle
  to reconnect. Legitimate clients reconnect with their still-valid
  certs; the attacker's reconnect attempt presents the now-revoked
  cert and is refused. This is the recommended incident-response
  path whenever there is any suspicion of active misuse.
- **Tenant certs**: `tenant:` certs are shared across many nodes
  by design, so a single exfiltration compromises the entire
  tenant until rotation. `revoke-client --tenant` still works — it
  advances the watermark for the shared CN — but the replacement
  cert must be redistributed to every node in the tenant before
  any of them can reconnect. This is the intrinsic cost of a
  shared credential, not a limitation of the watermark mechanism.

File permissions (mode 0400, agent-owned) remain the first line of
defense against casual exfiltration. The watermark mechanism is the
response when that line fails. See
[Key Compromise Scenarios](#key-compromise-scenarios) for the
operational procedure.

## Certificate Hierarchy

### Why intermediate CAs per pool

Several designs were considered:

| Design | Advantage | Drawback |
|--------|-----------|----------|
| Single system-wide CA signs all node certs | Simple: one CA, one trust root. | No per-pool isolation. A node cert is valid for every pool. |
| Pool property stores a list of allowed node cert fingerprints | Per-pool isolation without PKI. | List grows linearly with nodes. Thousands of nodes = impractical. |
| Pool property stores intermediate CA cert(s) as a PEM bundle | Adding nodes means issuing certs -- no property update. Bundle size is independent of node count. Supports CA rotation via overlap. | Requires PKI infrastructure to issue and manage per-pool intermediate CAs. |

The intermediate CA design (option 3) was chosen because it scales
cleanly and standard X.509 chain validation handles the trust
relationship. The PEM bundle format allows multiple CAs to coexist
during a rotation window.

**Important**: The pool CA certificate must be a valid intermediate CA —
it must have `BasicConstraints: CA=true` and `KeyUsage: keyCertSign`.
Without these, Go's `x509.Certificate.Verify` will reject the chain
with a confusing error. `dmg pool set-cert --daos-ca-key` produces correct
certs automatically; this matters when using externally-generated CAs.

### What the pool property stores

`DAOS_PROP_PO_POOL_CA` stores a **PEM bundle** containing one or more
public certificates of the pool's intermediate CAs. This is not
sensitive data. Pool CA **private keys** are NOT stored in DAOS -- they
are produced by `dmg pool set-cert`, written to the admin's output
directory, and used only when issuing node certificates.

During CA rotation, the bundle temporarily contains two CAs (old and
new). Validation succeeds if the node cert chains to any CA in the
bundle. Once all nodes have been re-issued certs from the new CA, the
old CA is removed from the bundle.

### Chain of trust

```
DAOS CA (root)
  |
  |-- signs --> Pool CA cert (intermediate, in pool property bundle)
  |                |-- signs --> node1.crt
  |                |-- signs --> node2.crt
  |
  |-- signs --> Pool CA cert for another pool
                   |-- signs --> ...
```

The server validates: node cert -> any pool CA in bundle -> DAOS CA.
Standard X.509 chain validation via Go's `x509` package.

## Proof-of-Possession

### Why it's needed

CART does not use TLS -- messages travel in cleartext on the fabric.
Simply presenting a certificate doesn't prove anything: anyone who can
observe fabric traffic can copy the certificate bytes. The client must
prove it holds the corresponding private key.

### How it works

The agent constructs a payload, signs it with the node's private key,
and sends the signature and the timestamp (but not the payload itself)
alongside the certificate:

```
payload = pool_uuid (16 bytes) || handle_uuid (16 bytes)
       || timestamp (8 bytes)  || sha256(cred) (32 bytes)   -- 72 bytes
```

Only the timestamp travels on the wire as a distinct field. The other
three pieces are things the server already has in the same RPC:
`pool_uuid` and `handle_uuid` come from `pci_op.pi_uuid` / `pci_op.pi_hdl`,
and `sha256(cred)` is computed over `pci_cred`. The server reconstructs
the payload byte-for-byte from those sources and verifies the signature
against it.

### Why each payload field exists

| Field | Purpose |
|-------|---------|
| **pool_uuid** | Binds PoP to a specific pool. Prevents a captured PoP from being replayed against a different pool. |
| **handle_uuid** | Binds PoP to a specific connect attempt. Enables handle-UUID dedup. |
| **timestamp** | Limits validity window. Server rejects timestamps outside configurable skew (default: 5 min). |
| **sha256(cred)** | Binds PoP to the specific AUTH_SYS credential it was issued with. Prevents a fabric attacker who captures two legitimate sessions from the same node from mixing one session's PoP with another session's credential. |

### Why the server reconstructs the payload instead of receiving it

An earlier version of the design had the agent send the full 72-byte
payload on the wire and the server compare individual fields (`payload.
pool_uuid == pci_op.pi_uuid`, etc.) before verifying the signature.
That works but makes correctness depend on getting every cross-check
right, forever.

Reconstructing the payload from the RPC fields makes the binding
structural:

- There is no alternate `pool_uuid` or `handle_uuid` the attacker can
  put in the "signed payload" vs. the RPC — the signed bytes *are* the
  RPC's fields by construction.
- The `sha256(cred)` binding is always computed over `pci_cred`, so
  swapping in a different captured credential automatically breaks
  signature verification. There is no cross-check to forget.
- The wire cost drops from an iov of 72 bytes to a single `uint64`, and
  CART marshals one fewer field on the hot path of pool connect.

### The credential-swap attack this closes

Without `sha256(cred)` in the payload, a fabric attacker observing two
concurrent legitimate pool-connects from the same node as different
users (`cred_A, PoP_A, handle_A`) and (`cred_B, PoP_B, handle_B`) can
attempt `(cred_B, cert, PoP_A, handle_A)` during the narrow window
before the legitimate `handle_A` commits to RDB. All of the other
server-side checks pass — cert chains, CN cross-validates against
`cred_B.machine_name` (same hostname), PoP signature verifies over
the payload `PoP_A` was signed over — and the attacker obtains a
handle as user B using the PoP that was produced for user A.

Binding `sha256(pci_cred)` into the payload collapses this: the server
computes the hash over whatever `pci_cred` arrives in the RPC, and any
swap changes the hash, which invalidates the signature.

### Signing algorithms

The payload is hashed before signing.

| Key type | Algorithm | Hash |
|----------|-----------|------|
| RSA | RSA-PSS | SHA-512 |
| ECDSA P-256 | ECDSA | SHA-256 |
| ECDSA P-384 | ECDSA | SHA-384 |

The server determines the algorithm from the certificate's public key.
No algorithm identifier is sent in the RPC -- this avoids algorithm-
confusion attacks. RSA uses SHA-512 to match existing DAOS credential
signing. ECDSA hash sizes match their curve security levels. The
default key type is ECDSA P-384; RSA and P-256 are accepted for
external PKI interoperability.

## Replay Protection

### The attack

An attacker with fabric access captures a legitimate pool connect RPC
and replays it. Without protection, the replay succeeds because the PoP
signature is still valid.

### Defense: timestamps + handle UUID dedup

**Timestamp window**: The server rejects PoP with timestamps outside
`pool_cert_max_clock_skew` (default: 5 minutes).

**Handle UUID dedup cache**: After validating a PoP, the server records
the handle UUID in an in-memory cache. Duplicates within `2 * skew` are
rejected. The `2x` factor covers the worst case: a request at `T - skew`
replayed at `T + skew`.

Entries are evicted after `2 * skew`. The cache is not persistent --
lost on restart, which is acceptable because any replay must still pass
the timestamp check. The vulnerability window is bounded by
`min(downtime, 2 * skew)` and requires a pre-captured RPC.

### Why not server-issued nonces?

Nonces add a round trip before every pool connect. HPC clusters have
synchronized clocks (NTP/PTP), making timestamps practical.

### Interaction with existing handle uniqueness

The pool connect handler already rejects duplicate handle UUIDs at RDB
commit. The dedup cache covers the race window before commit and the
case where the original connect failed but the replay succeeds. Clients
generate a new handle UUID per attempt, so legitimate retries never
collide with the cache.

## Validation Chain

```
pool_connect_handler (srv_pool.c)
  |
  |-- 1. Load pool properties
  |-- 2. Extract credential machine name (ds_sec_cred_get_origin)
  |
  |-- 3. Does pool have DAOS_PROP_PO_POOL_CA set?
  |       +-- No:  Skip cert check, proceed to credential/ACL check
  |       +-- Yes: Continue
  |
  |-- 4. Is the RPC protocol version >= 8?
  |       +-- No:  Return -DER_NO_CERT
  |       +-- Yes: Continue
  |
  |-- 5. ds_sec_validate_node_cert() [engine, C]
  |       |
  |       +-- Cert present in RPC?
  |       |     No:  Return -DER_NO_CERT
  |       |     Yes: Build dRPC request with pool CA bundle,
  |       |          machine name, cert, PoP fields, and the
  |       |          DAOS_PROP_PO_CERT_WATERMARKS blob as an
  |       |          opaque byte array (no parsing in C)
  |       |
  |       +-- processValidateNodeCert() [control plane, Go]
  |             +-- 5a. Parse node cert             -> -DER_BAD_CERT
  |             +-- 5b. Parse pool CA bundle        -> -DER_BAD_CERT
  |             +-- 5c. Load DAOS CA                -> -DER_NO_CERT
  |             +-- 5d. Chain validation            -> -DER_BAD_CERT
  |             |       (cert chains to any CA in
  |             |       bundle; expiration; EKU)
  |             +-- 5e. Cross-validate cert CN      -> -DER_BAD_CERT
  |             |       node: suffix must match
  |             |       credential machine name;
  |             |       tenant: skip; no prefix:
  |             |       reject
  |             +-- 5f. Cert revocation check       -> -DER_BAD_CERT
  |             |       Decode watermarks JSON blob;
  |             |       if watermarks[cn] exists and
  |             |       cert.NotBefore < watermark
  |             |       -> cert revoked. Malformed
  |             |       blob or bad timestamp also
  |             |       rejects (fail-closed).
  |             +-- 5g. Validate payload length     -> -DER_INVAL
  |             +-- 5h. Check timestamp skew        -> -DER_NO_PERM
  |             +-- 5i. Verify PoP signature        -> -DER_NO_PERM
  |             +-- 5j. Check handle UUID dedup     -> -DER_NO_PERM
  |             +-- Success
  |
  |-- 6. Credential/ACL check (unchanged)
  |-- 7. Pool connect proceeds
```

### Why this ordering

- **Step 2**: Extract machine name (already done in existing code for
  `ph_machine`). No new work.
- **Step 3**: Skip all cert processing on pools that do not require
  certs. Matches the "no measurable performance impact on non-cert
  pools" requirement.
- **Step 4**: Reject old clients before any cert work.
- **Step 5 (presence check) in engine**: Avoids dRPC for missing cert.
- **5a-5d before cross-validation/revocation/PoP**: Don't run any
  downstream check on a cert that fails chain validation.
- **5e (cross-validation) after chain validation**: Verify cert CN
  matches credential machine name. A cheap string comparison. Note
  that this check is satisfied by any attacker who controls the
  client: they set the AUTH_SYS credential's machine name to match
  the cert CN suffix. It is *not* a security boundary against
  exfiltration — it catches misdeployment and credential
  fabrication mistakes. The revocation check below is what makes
  exfiltration recoverable.
- **5f (revocation) after cross-validation**: JSON decode is more
  expensive than string compare, so run the cheap check first. The
  revocation check is the only server-side mechanism that
  cryptographically invalidates an exfiltrated cert; all the earlier
  checks can be passed by an attacker-controlled client. Fail-closed
  behavior on a malformed or missing-but-referenced blob: if the
  blob is present and cannot be decoded, reject with
  `-DER_BAD_CERT` rather than treating it as "no revocations".
- **5h (timestamp) before signature**: Trivial vs. expensive.
- **5j (dedup) after signature**: Prevent cache poisoning with
  garbage.

## Key Compromise Scenarios

### Node private key compromised

**Exposed**: Attacker can forge PoP for pools the node has certs for.
If the private key was exfiltrated, the attacker can do so from any
machine, not just the original node.

**Response**:
1. Revoke the cert on every affected pool, paired with a pool-wide
   handle eviction so any live session the attacker may hold is
   dropped:
   ```
   dmg pool revoke-client POOL \
       --pool-ca-key /secure/pool_keys/<pool_uuid>_ca.key \
       --node <compromised-node> \
       --output /secure/replacement \
       --evict-all-handles
   ```
   This atomically issues a fresh replacement cert, advances the
   per-CN watermark so the old cert is cryptographically dead, and
   evicts every active handle on the pool. The old cert cannot be
   used to reconnect regardless of which machine presents it or
   what hostname that machine claims.
2. Investigate and remediate the compromised node.
3. Distribute the replacement cert from `/secure/replacement/<node>/`
   to the remediated node and restart its agent. The new cert's
   `NotBefore` equals the watermark, so it passes the revocation
   check on the legitimate node while the exfiltrated copy remains
   rejected.
4. Only rotate the pool CA if the **pool CA private key** itself is
   suspected compromised. A leaf-cert compromise is handled entirely
   by revocation; CA rotation is not required.

**Blast radius**: Only pools the node had certs for. Revocation
provides immediate cryptographic containment — no dependency on the
attacker's hostname, no reliance on the agent-side CN check, no
race window between "block" and "unblock". The runbook step that
closes the breach is the same step that restores service on the
remediated node.

### Pool CA private key compromised

**Exposed**: Attacker can issue node certs for that one pool.

**Response**: Generate a new pool CA, re-issue node certs for legitimate
nodes.

**Blast radius**: One pool. Other pools unaffected.

### DAOS CA private key compromised

**Exposed**: System-wide. Attacker can create any pool CA.

**Response**: Same as any DAOS CA compromise -- regenerate everything.
This is an existing operational concern; node auth doesn't make it
worse.

### Certificate expiration

Default validity: 1 year. When a cert expires:
- **Agent** detects expiration on load and refuses to use it.
- **Server** chain validation rejects it.
- Result: `-DER_BAD_CERT`.

Proactive expiration warnings are planned as future work.

## Design Decisions

### Why not TLS on the fabric?

CART doesn't implement TLS. Adding it would require significant effort
and impact every I/O operation. Node auth operates at the application
level, only at pool connect time -- negligible performance impact.

### Why per-CN watermarks instead of a traditional CRL?

Traditional CRL requires the CA to publish a signed list of revoked
certificate serial numbers. OCSP requires a responder service. Both
require **tracking which serial numbers were issued to which nodes**
— effectively building certificate lifecycle management into DAOS,
which is out of scope.

A hostname-based denylist (an earlier version of this design) avoids
the tracking problem by operating on client identity rather than
certificate identity. It fails in the exfiltration scenario: the
machine name in the AUTH_SYS credential is trivially set by an
attacker-controlled agent, and the only hostname you can actually
denylist is also the hostname of the legitimate node, which means
the breach is closed only while service is broken and re-opens the
instant the legitimate node is unblocked.

Cert watermarks take a third path: **revoke by identity watermark,
not by serial number**. Each pool stores a map of `CN → RFC3339
timestamp` in the `DAOS_PROP_PO_CERT_WATERMARKS` property. On every
pool connect (for a pool with node auth), the server compares the
presented cert's `NotBefore` against the watermark for the cert's
CN, and rejects any cert whose `NotBefore` is strictly less than
the watermark. `dmg pool revoke-client --node X` atomically issues
a fresh cert (with `NotBefore == now()`), advances the pool's
watermark for `node:X` to the same instant, and writes the updated
blob. The old cert is dead immediately; the new cert passes the
equal-timestamp check and survives.

**Why this avoids the CRL state problem**: the map is keyed by CN,
not by serial number. There is exactly one entry per *identity that
has ever been revoked*, not one per cert ever issued. Entries are
bounded by the population of node/tenant identities, not by the
number of historical revocations. DAOS never needs to know which
serial numbers it issued.

**Why this closes the exfiltration gap**: the two values being
compared — `cert.NotBefore` and `watermark[cn]` — are facts the
attacker cannot both control.

- `cert.NotBefore` is **baked into the cert at issuance time by the
  pool CA**. Forging a different `NotBefore` requires the pool CA
  private key, which lives off-cluster on the admin host and is
  never exposed to client nodes.
- `watermark[cn]` is a **server-side pool property**, set only by an
  authenticated `dmg` call. An attacker cannot write it without
  admin credentials.

So when those two values disagree, "this cert was issued before the
most recent revocation event for this identity" is a fact the
server can determine without trusting the agent, without trusting
the network, without trusting the attacker's hostname claim, and
without tracking which serial numbers it has ever signed.

**Where monotonicity is enforced**: The invariant "a revocation must
never lower any existing CN's watermark" is enforced in the
`PoolRevokeClient` gRPC handler on the management service leader,
under the same pool lock that serializes the other pool-state
mutations. The handler reads the current blob, computes the next
watermark via `security.AdvanceCertWatermark` (which clamps `now()` to
`prev + 1s` when the wall clock would otherwise retreat), writes
the updated blob via the engine's `PoolSetProp` dRPC, and returns
the committed timestamp to the caller. The caller then binds that
exact timestamp into the replacement cert's `NotBefore`.

The generic `PoolSetProp` gRPC handler refuses
`DAOS_PROP_PO_CERT_WATERMARKS` (and `DAOS_PROP_PO_POOL_CA`), so the
read-modify-write cannot be bypassed by a hand-crafted gRPC client,
an older `dmg`, or a `dmg pool set-prop` typo. The client-side
helper in `src/control/lib/control/pool_cert.go` is purely
ergonomic: it builds the RPC request and signs the replacement cert
around whatever watermark the server returns; it does not own the
invariant.

Keeping all JSON parsing on the Go side also keeps the engine
simple. The engine treats the watermarks blob as opaque bytes and
forwards it in both directions over dRPC; nothing in C parses,
compares, or validates the map contents.

**Why a JSON blob instead of a structured RDB key**: The pool
property layer already supports variable-length byte arrays via the
existing `DAOS_PROP_PO_POOL_CA` plumbing. Storing the watermarks as
one more byte-array prop reuses that plumbing without adding a new
keyspace to the RDB layout. The engine never parses the blob, so
the JSON format is chosen for debuggability (`dmg pool get-prop` on
a devbox produces human-readable output) rather than performance.
A flat map `{CN: RFC3339}` is the simplest format that encodes the
required information; RFC3339 is the format the control library
produces from `time.Time.Format` and consumes with `time.Parse`,
with no custom parser on either side.

**Limitation**: The watermark cannot distinguish between two
certificates for the same CN with different `NotBefore` values
other than by which one is older. This is the correct behavior for
the expected use case (revoke all outstanding certs for an identity
and issue one new one), but it means you cannot keep multiple
active certs for the same CN in flight simultaneously and
selectively revoke one of them. In practice, cert issuance for a
given identity is never overlapped — you copy the new cert over
the old one on disk — and the pool CA bundle handles overlap at
the *CA* level, which is where you actually want it.

**Limitation (tenant certs)**: Revoking a tenant cert invalidates
the single shared cert for *all* tenant members, so the
replacement must be redistributed to every member before any of
them can reconnect. This is intrinsic to any shared-credential
scheme and no revocation mechanism can sidestep it. For
fine-grained revocation, use node-scoped certs instead.

### Why ExtKeyUsageClientAuth?

Node certs are generated with `ClientAuth` extended key usage. The
server requires this during validation, preventing certificates
intended for other purposes from being used as node certs.

### Why node certs are separate from agent certs

DAOS agents already have a certificate (`agent.crt`) used for gRPC mTLS
with the control plane and for credential signature verification. These
are separate because they answer different questions, operate in
different trust chains, and have different lifecycles:

| | Agent cert | Node cert |
|---|-----------|-----------|
| **Question** | "Is this a legitimate DAOS agent?" | "Is this node authorized for this pool?" |
| **Protocol** | gRPC mTLS (transport-level) | Pool connect PoP (application-level) |
| **Signed by** | DAOS CA (directly) | Intermediate CA (pool-specific) |
| **Cardinality** | One per system (shared by all agents) | N pool-specific per node |
| **Lifecycle** | Deployed at system setup, rarely changes | Issued as pools are created, rotated independently |
| **Revocation effect** | Agent loses management channel access | Node loses pool access |

See [Future Directions](#future-directions) for the credential signing
unification approach that could collapse these.

## Future Directions

### System-level node authentication

A system-wide client CA that applies to all pools without a pool-specific
CA would provide:

- **Per-node cryptographic identity**: Each node has its own cert
  (CN = node name), unlike the shared agent cert.
- **Key isolation**: Stealing one node's key compromises only that node,
  unlike the shared agent key where any node's copy is everyone's copy.

This is deferred because the shared agent cert already prevents
unauthorized nodes from connecting (no valid agent credentials = no
valid tokens), and the system-level tier requires careful design of CA
distribution (server config file? system property? well-known path?)
and fallback semantics (pool CA takes precedence over system CA).

### Credential signing unification

A more fundamental evolution: use the node certificate's private key to
sign AUTH_SYS credentials instead of the shared agent key. This would
collapse credential verification and node authentication into a single
signature -- the credential signature itself becomes the proof of
possession.

This is appealing because it eliminates the separate PoP mechanism and
could subsume both system-level and per-pool auth. However, it touches
the gRPC auth middleware, credential origin/verification path, and
operations that lack pool context (container ops, management queries).
The scope exceeds this feature.

## Fabric Trust Model

DAOS assumes a **trusted fabric** — no encryption or integrity
protection on RPCs. Node auth operates within this same trust model.
An attacker with fabric access can:

- Observe certificates and PoP signatures (but cannot forge new
  signatures without the private key).
- Replay captured RPCs (mitigated by timestamp + dedup).
- Modify non-signed fields in a captured RPC while forwarding the
  original PoP signature intact. PoP proves key possession, not
  full-RPC integrity.

Node auth **does not upgrade the fabric trust model**.

## Glossary

| Term | Meaning |
|------|---------|
| **CA** | Certificate Authority — an entity that issues and signs certificates. |
| **CN** | Common Name — the primary subject field of a certificate. DAOS node certs use `CN=node:<hostname>`; tenant certs use `CN=tenant:<name>`. |
| **CRL** | Certificate Revocation List — a published list of revoked certificates. |
| **ECDSA** | Elliptic Curve Digital Signature Algorithm — a signature scheme using elliptic curve keys. |
| **mTLS** | Mutual TLS — TLS where both client and server present certificates. |
| **NotBefore** | X.509 validity field giving the earliest time at which a certificate is valid. Embedded into the cert by the signing CA and covered by the CA's signature. |
| **OCSP** | Online Certificate Status Protocol — a service for checking certificate revocation status in real time. |
| **PKI** | Public Key Infrastructure — the system of CAs, certificates, and keys used to establish trust. |
| **PoP** | Proof-of-Possession — a signature proving the sender holds a private key corresponding to a certificate. |
| **Revocation watermark** | Per-CN timestamp stored in `DAOS_PROP_PO_CERT_WATERMARKS`. Any cert for that CN whose `NotBefore` is strictly less than the watermark is rejected at pool connect. Advanced by `dmg pool revoke-client`. |
| **RSA-PSS** | RSA Probabilistic Signature Scheme — a modern RSA signing mode that avoids weaknesses in older (PKCS#1 v1.5) padding. |
| **X.509** | The standard format for public key certificates, used by TLS and most PKI systems. |

[design]: node_authentication.md
