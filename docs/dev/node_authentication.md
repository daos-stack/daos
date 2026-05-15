# Client Node Authentication

Epic: [DAOS-18783](https://daosio.atlassian.net/browse/DAOS-18783)

## Introduction

DAOS authenticates client nodes on the control plane via mTLS, but a
single system-wide CA issues every agent certificate, so any client
node trusted by the system can attempt to reach any pool. User-level
ACLs are the only thing gating access beyond that, and a root
compromise on a client node lets an attacker impersonate any local
user, which effectively exposes every pool those users can reach.

This feature introduces per-pool client node authentication: each pool
can be configured with its own intermediate CA, and only client nodes
presenting a certificate signed by that CA may connect. This caps the
blast radius of a client node compromise to the pools whose CAs have
signed that node's certificate, independent of which users happen to
be logged in. Shared tenant certificates can be distributed to a group
of client nodes to scope them to a specific set of pools.

Client node authentication is orthogonal to user authentication. It
answers "is this client machine allowed to connect to this pool?",
which is distinct from "who is this user and are they allowed to
access this pool?"

### Relationship to Multiple Authentication Types

The [Multiple Authentication Types][multiauth] effort introduces a
pluggable authentication framework to support identity providers
beyond AUTH_SYS. That work answers "who is this user?"

These are complementary:

- Node certificates gate access at the machine level.
- Authentication flavors establish user identity within that connection.
- A pool connect must pass both checks: valid node certificate (if
  required), then valid user credential with sufficient ACL permissions.

This feature can ship independently of the multi-auth effort.

## Requirements

### Functional

1. An administrator must be able to configure a pool to require node
   certificates for access.
2. A pool connect from a node without a valid certificate for that pool
   must be rejected before credential/ACL evaluation.
3. A pool connect from a node with a valid certificate must proceed to
   normal credential/ACL evaluation.
4. Pools without a configured certificate requirement must behave
   exactly as they do today.
5. Node certificate private keys must be accessible only to the DAOS
   agent, not to client processes.
6. Node certificates must chain to the pool's intermediate CA, which
   itself chains to the existing DAOS CA.
7. The feature must work with AUTH_SYS and be compatible with future
   authentication flavors.
8. An administrator must be able to revoke a specific node or tenant
   certificate on a pool. Revocation must cryptographically invalidate
   the existing cert (not merely block the hostname that presented
   it), must issue a fresh replacement cert in the same operation, and
   should optionally evict active pool handles so the breach is
   contained in a single command.
9. The agent validates the certificate CN prefix as a defense-in-depth
   check against misdeployment: node-scoped certs (`node:hostname`)
   must match the local hostname; tenant-scoped certs (`tenant:name`)
   skip the hostname check. The CN check is not a security boundary
   against an attacker who controls the client machine — see the
   [Security Design][security] for the full trust analysis.
10. An administrator must be able to issue tenant-scoped certificates
    that are shared across multiple nodes, enabling tenant-level pool
    access control.

### Non-functional

1. No measurable performance impact on pool connect for pools that do
   not require node certificates.
2. Minimal performance impact for pools that do require node
   certificates (one additional certificate chain validation and
   signature verification, comparable to existing credential validation).
3. Certificate generation and distribution are out-of-band
   administrative tasks (expected to be automated by the appliance
   framework).
4. Wire protocol changes must be backward-compatible: older clients
   connecting to pools without certificate requirements must work
   unchanged.

## Threat Model

Per-pool node certificates limit the blast radius of a client node
compromise: an attacker can only access pools for which the compromised
node has been issued a certificate. For pools that opt in to node
authentication, each pool has its own CA, so cross-pool lateral movement
is prevented cryptographically. Pools without a configured CA remain
open to any node on the fabric, exactly as they are today.

The feature does not expand the agent trust boundary (the agent already
signs user credentials) and does not address user-level impersonation
(that remains the job of credentials + ACLs).

See the [Security Design][security] for the full threat analysis, trust
model, key generation trust boundaries, and compromise response
procedures.

## Design Overview

### Certificate Hierarchy

```
DAOS CA (existing, system-wide)
  |
  +-- Pool CA for "foo" (intermediate cert, public stored as pool property)
  |     +-- CN=node:node1  <pool_uuid>.crt  (node-scoped)
  |     +-- CN=node:node2  <pool_uuid>.crt  (node-scoped)
  |     +-- CN=tenant:teamA <pool_uuid>.crt (tenant-scoped, shared across nodes)
  |
  +-- Pool CA for "bar" (intermediate cert, public stored as pool property)
        +-- CN=node:node3  <pool_uuid>.crt
        +-- CN=node:node4  <pool_uuid>.crt
```

Each pool gets a **pool-specific intermediate CA** signed by the
existing DAOS CA. The pool property stores only the intermediate CA's
public certificate as a PEM bundle, which can contain multiple CAs
during a rotation window. Adding or removing clients requires only
issuing or revoking individual certificates signed by that pool
certificate, not updating the pool property.

The pool CA's private key is used to issue node certificates and should
be protected accordingly (restricted access, encrypted at rest) because
compromising it allows the issuing of node certs for any machine.

### High-Level Flow

**Pool Connect (with node auth)**

```
Client                     Agent                      Server
  |                          |                           |
  | dc_sec_request_pool_     |                           |
  |   creds(pool, handle)    |                           |
  |----------dRPC----------->|                           |
  |                          | 1. Generate AUTH_SYS      |
  |                          |    credential as today    |
  |                          | 2. Lookup node cert for   |
  |                          |    target pool            |
  |                          | 3. Validate cert CN prefix|
  |                          |    node: must match       |
  |                          |    gethostname(); tenant: |
  |                          |    skip hostname check    |
  |                          | 4. Sign proof-of-         |
  |                          |    possession with node's |
  |                          |    private key            |
  |<-----credential +--------|                           |
  |      node_cert +         |                           |
  |      pop_signature       |                           |
  |                          |                           |
  |---------POOL_CONNECT RPC (cred + node_cert + PoP)--->|
  |                          |                           |
  |                          |     5. Pool has CA prop?  |
  |                          |        If no: skip check  |
  |                          |        If yes:            |
  |                          |          a. Cert present? |
  |                          |          b. Cert chains   |
  |                          |             to any CA in  |
  |                          |             the bundle?   |
  |                          |          c. Not expired?  |
  |                          |          d. node: cert CN |
  |                          |             matches cred  |
  |                          |             machine name? |
  |                          |             tenant: skip  |
  |                          |          e. PoP valid?    |
  |                          |     6. Validate AUTH_SYS  |
  |                          |        credential         |
  |                          |     7. Check pool ACL     |
  |<--------------------result---------------------------|
```

### Reconnect Behavior

Node certificate validation occurs only on initial `POOL_CONNECT`. Once
a handle is established, it remains valid across leader changes and
server restarts, meaning that reconnect does not re-validate the node
cert. This means that deleting a node's cert or rotating the pool CA
does not affect active handles by itself.

`dmg pool revoke-client` advances the per-CN watermark to
cryptographically invalidate the old cert for future connects, but
does not by itself terminate existing sessions. The `--evict-all-handles`
flag pairs revocation with a pool-wide `dmg pool evict`, which drops
every active handle on the pool. Legitimate clients reconnect with the
new cert that `revoke-client` just issued (and which survives the
watermark check because its `NotBefore` equals the watermark);
attacker connects fail because the old cert is now strictly below the
watermark for its CN. The eviction is pool-wide rather than
node-targeted because the handle structure does not record which cert
was used to open it, and an attacker with a fabricated machine name
would evade a hostname-filtered eviction.

### Proof-of-Possession

CART does not use TLS, so presenting a certificate alone proves nothing,
and the client must prove possession of the corresponding private key.
The agent signs a proof-of-possession (PoP) payload binding the pool
UUID, handle UUID, a timestamp, and a hash of the AUTH_SYS credential
it issued for the same connect. The server verifies the signature using
the public key from the certificate. The timestamp provides replay
protection; the credential hash binds the PoP to the specific user
credential the agent vouched for.

Only the timestamp crosses the wire as a distinct field. The pool UUID
and handle UUID are already present in the `POOL_CONNECT` RPC, and the
credential hash is recomputed server-side from `pci_cred`. The server
reconstructs the 72-byte payload from those four pieces before
verifying the signature.

See the [Security Design][security] for the PoP payload format, signing
algorithms, replay protection mechanism, and design rationale.

## Detailed Design

### Pool Properties

Two new pool properties support node authentication:

```c
/* daos_prop.h */
DAOS_PROP_PO_POOL_CA         /* PEM bundle of pool intermediate CA certificate(s) */
DAOS_PROP_PO_CERT_WATERMARKS /* JSON map of CN -> RFC3339 revocation watermark */
```

#### DAOS_PROP_PO_POOL_CA

- **Type**: Variable-length byte array (PEM bundle -- one or more
  concatenated PEM-encoded X.509 CA certificates). Transported via
  the `byteval` proto field.
- **Default**: Empty (no node certificate required).
- **Set by**: the dedicated `PoolAddCA` and `PoolRemoveCA` gRPC
  handlers on the management service leader (invoked from `dmg pool
  set-cert` and `dmg pool delete-cert`). Each handler takes the pool
  lock, reads the current bundle, validates the modification, and
  writes the new bundle via the engine's `PoolSetProp` dRPC. The
  generic `PoolSetProp` gRPC handler refuses
  `DAOS_PROP_PO_POOL_CA` outright, so the bundle cannot be set
  through the generic property interface.
- **Mutable**: Yes. CAs can be added to or removed from the bundle.
  Does not affect already-connected handles.
- **Clearable**: Yes, via `dmg pool delete-cert --all` (calls
  `PoolRemoveCA` with `all=true`, which clears the bundle).
- **Validation on add**: the server parses the submitted PEM and
  rejects a cert that is not a usable CA (no `BasicConstraints: CA,
  true` or no `KeyUsage: KeyCertSign`). `dmg` additionally validates
  that the CA chains to the DAOS CA before submitting.
- **Semantics**: During validation, the server accepts a node cert that
  chains to **any** CA in the bundle. This allows CA rotation without
  a flag-day cutover (see [Rotation](#rotation)).

#### DAOS_PROP_PO_CERT_WATERMARKS

- **Type**: Variable-length byte array containing an opaque JSON
  object: a flat map from CN (including the `node:` / `tenant:`
  prefix) to an RFC3339 UTC timestamp. Example:
  `{"node:node1":"2026-04-15T14:00:29Z","tenant:teamA":"2026-04-20T09:15:00Z"}`.
  Transported via the `byteval` proto field.
- **Default**: Empty (no revocations).
- **Set by**: the dedicated `PoolRevokeClient` gRPC handler on the
  management service leader. Under the pool lock it reads the current
  blob, computes the new watermark for the target CN, writes the
  updated blob via the engine's `PoolSetProp` dRPC, and returns the
  committed timestamp to the caller. The generic `PoolSetProp` gRPC
  handler refuses `DAOS_PROP_PO_CERT_WATERMARKS` outright, so the
  read-modify-write path cannot be bypassed by a hand-crafted client.
- **Mutable**: Yes, but monotonic: a revocation may not lower any
  existing CN's watermark. `security.AdvanceCertWatermark` clamps `now()`
  to `prev+1s` when the wall clock would otherwise retreat, and the
  server-side handler is the only caller that writes the property.
- **Engine treatment**: Opaque. The engine stores and ships the blob
  verbatim to the control plane over dRPC; no C-side parsing,
  comparison, or validation. All semantics live in Go
  (`processValidateNodeCert` in `src/control/server/security_rpc.go`).
- **Validation semantics**: During node cert validation, the control
  plane decodes the blob, looks up the cert's CN, and rejects any
  cert whose `NotBefore` is **strictly less than** the watermark for
  that CN. A freshly-issued replacement cert from `revoke-client`
  has `NotBefore == watermark` and therefore passes. CNs not present
  in the map are unrestricted.
- **Consulted only for pools with a CA configured**: A pool without
  `DAOS_PROP_PO_POOL_CA` has no certs to revoke, so the watermark
  prop is never read on such pools. There is no non-cert equivalent
  of revocation; a pool without node auth is open to any node that
  can reach the fabric, exactly as it is today.

### dmg Interface

```bash
# Generate a pool-specific CA and add it to the pool's CA bundle
dmg pool set-cert POOL1 --daos-ca-key /etc/daos/certs/daosCA.key \
    --output /secure/pool_keys
# Produces:
#   /secure/pool_keys/<pool_uuid>_ca.crt  (pool intermediate CA cert)
#   /secure/pool_keys/<pool_uuid>_ca.key  (pool CA private key -- guard this)
# Appends the pool CA cert to the DAOS_PROP_PO_POOL_CA PEM bundle.

# Import a pre-existing pool CA cert (appends to bundle)
dmg pool set-cert POOL1 --cert /path/to/existing_pool_ca.crt

# Remove a specific CA from the bundle (by subject or fingerprint)
dmg pool delete-cert POOL1 --fingerprint <sha256>

# Remove all CAs (disables node auth for this pool)
dmg pool delete-cert POOL1 --all

# Show pool CA info (subject, fingerprint, not full PEM, for each CA in the bundle)
dmg pool get-cert POOL1

# Generate node certs for a specific pool
dmg pool add-client POOL1 --pool-ca-key /secure/pool_keys/<uuid>_ca.key \
    --node node1 --node node2 --output /tmp/certs/
# Output:
#   /tmp/certs/node1/<pool_uuid>.crt  (CN=node:node1)
#   /tmp/certs/node1/<pool_uuid>.key
#   /tmp/certs/node2/<pool_uuid>.crt  (CN=node:node2)
#   /tmp/certs/node2/<pool_uuid>.key

# Generate tenant certs (shared across nodes belonging to a tenant)
dmg pool add-client POOL1 --pool-ca-key /secure/pool_keys/<uuid>_ca.key \
    --tenant teamA --output /tmp/certs/
# Output:
#   /tmp/certs/teamA/<pool_uuid>.crt  (CN=tenant:teamA)
#   /tmp/certs/teamA/<pool_uuid>.key
# Deploy the same cert+key to all nodes belonging to teamA.

# Revoke a node: issues a replacement cert and advances the watermark
# for node:node1. The old cert is useless for pool connection from the
# moment the watermark commits.
dmg pool revoke-client POOL1 \
    --pool-ca-key /secure/pool_keys/<uuid>_ca.key \
    --node node1 \
    --output /tmp/revoked_certs/

# Pair revocation with a pool-wide eviction for incident response.
# Active handles are dropped; legitimate clients reconnect with
# their still-valid certs, the attacker's old cert is refused by
# the watermark check.
dmg pool revoke-client POOL1 \
    --pool-ca-key /secure/pool_keys/<uuid>_ca.key \
    --node node1 \
    --output /tmp/revoked_certs/ \
    --evict-all-handles

# Revoke a tenant. Requires redistributing the replacement cert to
# every tenant member before they can reconnect.
dmg pool revoke-client POOL1 \
    --pool-ca-key /secure/pool_keys/<uuid>_ca.key \
    --tenant teamA \
    --output /tmp/revoked_certs/

# Show revocation watermarks (CN -> earliest NotBefore still valid)
dmg pool list-revocations POOL1
```

`set-cert` generates an intermediate CA key pair, signs it with the
DAOS CA, **appends** the public cert to the pool's CA bundle, and
writes the key pair to `--output`. Both `--daos-ca-key` and `--output`
are required. If the pool already has a CA, the new CA is added
alongside it -- both are accepted during validation. This enables CA
rotation without a flag-day cutover.

`add-client` generates a unique key pair per target. `--node` creates
certs with `CN=node:<hostname>` (validated against hostname by the
agent); `--tenant` creates certs with `CN=tenant:<name>` (shared across
nodes, no hostname check). The flags are mutually exclusive. Default
validity is 1 year, capped at the pool CA's remaining validity. Output
is organized by name for distribution.

`revoke-client` accepts `--node` or `--tenant` (mutually exclusive)
and atomically (a) reads the pool's current `DAOS_PROP_PO_CERT_WATERMARKS`
blob, (b) computes a new watermark for that CN (strictly greater
than any previous watermark for the same CN), (c) generates a fresh
keypair and signs a replacement cert whose `NotBefore` equals the new
watermark, and (d) writes the updated blob back via `PoolSetProp`. The
replacement cert and key are written to `<output>/<name>/<pool_uuid>.{crt,key}`,
matching the layout used by `add-client` so the same distribution
tooling works. `--evict-all-handles` triggers a subsequent pool-wide
`dmg pool evict` after the watermark commit. The command only makes
sense on pools with node auth enabled (i.e. a `DAOS_PROP_PO_POOL_CA`
is set); there is no revocation mechanism for non-cert pools.

### Agent Configuration

```yaml
# daos_agent.yml

# Directory containing node certificates for this DAOS system.
# Default: /etc/daos/certs/<system_name>/node_certs/
node_cert_dir: /etc/daos/certs/daos_server/node_certs/
```

Directory layout:

```
/etc/daos/certs/daos_server/node_certs/
  <pool_uuid_1>.crt        # pool-specific cert
  <pool_uuid_1>.key
  <pool_uuid_2>.crt
  <pool_uuid_2>.key
```

- The agent looks up node certificates by pool UUID (the stable
  identifier; labels can change). For a pool with UUID
  `12345678-...`, it looks for `<node_cert_dir>/12345678-....crt`
  and `.key`.
- `dmg pool add-client` outputs files in this naming convention
  (organized into per-node subdirectories for distribution). The
  contents of a node's subdirectory are copied directly into the
  node's `node_cert_dir`.
- Certificate and key file permissions follow existing DAOS rules
  (cert: max 0644, key: max 0400).
- Certs are cached after first load; PoP is generated fresh each time
  (unique per connect).
- If no cert is found for a target pool, the agent returns the
  credential without cert/PoP fields. The server decides whether to
  reject (pool requires cert) or accept (pool has no CA property).

### Credential Request Changes

The dRPC credential request from client to agent is pool-aware.

#### Proto Changes

```protobuf
/* auth.proto */

message GetCredReq {
    string pool_id = 1;    // Pool UUID (empty for legacy compat)
    bytes handle_uuid = 2; // Pool handle UUID (for PoP payload binding)
}

message GetCredResp {
    int32 status = 1;
    Credential cred = 2;
    bytes  node_cert      = 3; // PEM-encoded node certificate (optional)
    bytes  node_cert_pop  = 4; // PoP signature (optional)
    uint64 node_cert_time = 5; // PoP payload timestamp (UTC seconds)
}
```

The full PoP payload is never transmitted. The agent returns only the
timestamp; the server reconstructs the payload from the RPC's pool
UUID, handle UUID, timestamp, and `sha256(pci_cred)`. See the [Security
Design][security] for why reconstruction is preferable to shipping the
payload.

#### Client Library Changes

A new `dc_sec_request_pool_creds()` function (`cli_security.c`, declared
in `include/daos/security.h`) handles pool-aware credential requests:

```c
int dc_sec_request_pool_creds(d_iov_t *creds, uuid_t pool_uuid,
                              uuid_t handle_uuid, d_iov_t *node_cert,
                              d_iov_t *node_cert_pop,
                              uint64_t *node_cert_time);
```

The original `dc_sec_request_creds()` is unchanged to avoid touching all
existing callers. `dc_pool_connect_internal()` (`pool/cli.c`) calls the
new function, passing the pool and handle UUIDs so the agent can bind
them into the PoP payload.

#### Agent Changes

`SecurityModule.HandleCall()` (`security_rpc.go`) extracts the pool ID
from `GetCredReq` (empty body accepted for legacy clients), generates
the AUTH_SYS credential as today, then looks up the node cert for the
target pool. If found, the agent performs two validations before use:

1. **CN validation**: The cert CN must carry a recognized prefix
   (`security.CertCNPrefixNode` or `security.CertCNPrefixTenant`).
   For `node:` certs, the suffix after the prefix must match
   `gethostname()`; mismatches are rejected. For `tenant:` certs,
   no hostname check is performed (the cert is shared across nodes).
   Certs with no recognized prefix are rejected outright. This
   prevents misdeployed or exfiltrated node certs from being used and
   ensures the credential's machine name is cryptographically anchored
   to the cert CN for node-scoped certs.
2. **Expiration check**: The cert must not be expired.

If both checks pass, the agent constructs the 72-byte PoP payload
(`pool_uuid || handle_uuid || timestamp || sha256(cred)` where `cred`
is the AUTH_SYS credential it just signed), signs the payload with the
node's private key, and returns the certificate, the signature, and
the timestamp in the response. The payload itself is not transmitted;
the server reconstructs it from the same four inputs. Certs are cached
after first load; the PoP is generated fresh for every connect. If no
cert is found, the credential is returned without cert/PoP fields --
the server decides whether to reject.

### Pool Connect RPC Changes

The `POOL_CONNECT` RPC input is extended (pool protocol v8):

```c
/* pool/rpc.h */
#define DAOS_ISEQ_POOL_CONNECT
    ((struct pool_op_in)   (pci_op)                CRT_VAR)
    ((d_iov_t)             (pci_cred)              CRT_VAR)
    ((uint64_t)            (pci_flags)             CRT_VAR)
    ((uint64_t)            (pci_query_bits)        CRT_VAR)
    ((crt_bulk_t)          (pci_map_bulk)          CRT_VAR)
    ((uint32_t)            (pci_pool_version)      CRT_VAR)
    /* New fields (v8+) */
    ((uint32_t)            (pci_padding)        CRT_VAR)
    ((d_iov_t)             (pci_node_cert)      CRT_VAR)  /* PEM node cert */
    ((d_iov_t)             (pci_node_cert_pop)  CRT_VAR)  /* PoP signature */
    ((uint64_t)            (pci_node_cert_time) CRT_VAR)  /* PoP timestamp */
```

The signed PoP payload is not carried on the wire. The server
reconstructs it from `pci_op.pi_uuid`, `pci_op.pi_hdl`,
`pci_node_cert_time`, and `sha256(pci_cred)`, then verifies the
signature in `pci_node_cert_pop` against that. This removes 32 bytes
of iov overhead per connect at 100k-client scale and makes the
pool/handle/credential bindings structural — there are no alternate
attacker-supplied fields for a server bug to miss cross-checking.

**Backward compatibility**: A separate v7 RPC definition omits the new
fields. The server checks the RPC protocol version before extracting
cert fields. Old clients connecting to cert-required pools receive
`-DER_NO_CERT` with a descriptive error message.

### Server Connect Handler Changes

`pool_connect_handler()` (`srv_pool.c`) adds a node certificate
validation step before the existing credential/ACL check. The
watermarks blob is read from the pool property alongside the CA
bundle and handed to the control plane without any engine-side
parsing:

```c
/* Grab the machine name for cross validation */
rc = ds_sec_cred_get_origin(credp, &machine);
if (rc != 0) {
    DL_ERROR(rc, DF_UUID ": unable to retrieve origin",
             DP_UUID(in->pci_op.pi_uuid));
    D_GOTO(out_map_version, rc);
}

/* Node cert validation -- only for pools with a CA configured */
ca_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_POOL_CA);
if (ca_entry != NULL && ca_entry->dpe_val_ptr != NULL) {
    struct daos_prop_byteval *ca_bv = ca_entry->dpe_val_ptr;
    d_iov_t  ca_iov, wm_iov;
    d_iov_t *wm_iov_p = NULL;

    /* Reject old clients that can't send cert fields */
    if (!rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_NODE_CERT)) {
        rc = -DER_NO_CERT;
        goto out_map_version;
    }

    d_iov_set(&ca_iov, ca_bv->dpb_data, ca_bv->dpb_len);

    /* Watermarks are opaque to the engine -- if present, wrap the
     * property byteval in an iov and forward to the control plane.
     */
    wm_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_CERT_WATERMARKS);
    if (wm_entry != NULL && wm_entry->dpe_val_ptr != NULL) {
        struct daos_prop_byteval *wm_bv = wm_entry->dpe_val_ptr;

        d_iov_set(&wm_iov, wm_bv->dpb_data, wm_bv->dpb_len);
        wm_iov_p = &wm_iov;
    }

    rc = ds_sec_validate_node_cert(
        pool_uuid,
        &ca_iov,              /* CA PEM bundle from pool property */
        machine,              /* credential machine name for cross-validation */
        wm_iov_p,             /* cert watermarks blob (opaque) */
        node_cert_p,          /* node cert from RPC input */
        node_cert_pop_p,      /* PoP signature from RPC input */
        node_cert_payload_p   /* signed payload from RPC input */
    );
    if (rc != 0) {
        D_ERROR("Node certificate validation failed: "DF_RC"\n",
                DP_RC(rc));
        goto out_map_version;
    }
}

/* Existing credential/ACL check continues here */
rc = ds_sec_pool_get_capabilities(flags, &cred, &owner, acl, &sec_capas);
```

Note: `ds_sec_cred_get_origin()` already exists and is already called
in `pool_connect_handler()` to extract the machine name for
`ph_machine`.

#### Validation Logic

`ds_sec_validate_node_cert()` in `security/srv_acl.c` checks that a
cert is present in the RPC, then delegates to the Go control plane
via `DRPC_METHOD_SEC_VALIDATE_NODE_CERT`. The dRPC request carries
the cert, PoP, pool CA bundle, credential machine name, and the
opaque cert watermarks blob. The control plane performs chain
validation against the CA bundle, cross-validates the cert CN
against the credential machine name, parses the watermarks JSON and
applies the revocation check, verifies PoP, and checks for replays,
which is the same engine/control-plane split used for AUTH_SYS
credential validation.

See the [Security Design][security] for the full validation chain,
ordering rationale, engine/control-plane split, and revocation
semantics.

### Error Handling

| Condition | Error | Remediation |
|-----------|-------|-------------|
| Pool requires cert, client has none | `-DER_NO_CERT` | Install a valid cert on the client |
| Pool requires cert, cert chain invalid | `-DER_BAD_CERT` | Check pool CA configuration |
| Pool requires cert, cert expired | `-DER_BAD_CERT` | Obtain a fresh cert |
| Pool requires cert, node: cert CN != credential machine name | `-DER_BAD_CERT` | Cert is deployed on wrong host |
| Pool requires cert, cert CN has no recognized prefix | `-DER_BAD_CERT` | Cert was not generated by `dmg` |
| Pool requires cert, cert revoked (NotBefore < watermark for CN) | `-DER_BAD_CERT` | Obtain a fresh cert via `dmg pool revoke-client` |
| Pool requires cert, watermarks blob malformed | `-DER_BAD_CERT` | Admin must fix pool property |
| Pool requires cert, PoP clock skew too large | `-DER_NO_PERM` | Sync clocks |
| Pool requires cert, PoP signature invalid | `-DER_NO_PERM` | Cert or key may be corrupted; fix certificates |
| Pool requires cert, replay detected | `-DER_NO_PERM` | Retry (new handle UUID is generated automatically) |
| Pool has no CA requirement, client sends cert anyway | Success | None; cert is ignored |
| Old client connects to cert-required pool | `-DER_NO_CERT` | Upgrade client |

Validation failures are logged at ERROR with cert subject, pool UUID,
and failure reason for operator debugging.

### Certificate Lifecycle

#### Initial Setup

1. Generate a pool CA and set it on the pool:
   `dmg pool set-cert POOL --daos-ca-key /etc/daos/certs/daosCA.key --output /secure/pool_keys/`
2. Generate node certs:
   `dmg pool add-client POOL --pool-ca-key /secure/pool_keys/<uuid>_ca.key --node node1 --node node2 --output /tmp/certs/`
3. Securely distribute each node's cert and key to its `node_cert_dir`
   (key material must be protected in transit).

#### Adding a Node

```bash
dmg pool add-client POOL --pool-ca-key /path/to/pool_ca.key \
    --node newnode --output /path/to/output
# Distribute output to the node's node_cert_dir.
```

No pool property change is needed because the pool CA already trusts
certs it signs.

#### Removing a Node

To decommission a node cleanly (no compromise suspected), delete the
node's cert/key from its `node_cert_dir` and restart the agent. The
agent caches certs in memory, so deleting the files alone is not
sufficient. The pool property retains no reference to specific
certs, so no pool-side change is required.

If key compromise is suspected, revoke the cert instead:

```bash
dmg pool revoke-client POOL \
    --pool-ca-key /secure/pool_keys/<pool_uuid>_ca.key \
    --node compromised-node \
    --output /secure/replacement \
    --evict-all-handles
```

This single command atomically (a) issues a fresh replacement cert
for `node:compromised-node`, (b) advances the pool's per-CN watermark
to cryptographically invalidate the previously-issued cert, and
(c) evicts every active handle on the pool so any session the
attacker may already hold is terminated. The previous cert is dead
regardless of which machine presents it, whether the presenter
spoofs the hostname, or whether the PoP signature is otherwise
valid — the watermark check in `processValidateNodeCert` rejects it
before PoP verification runs.

Distribute the replacement cert from `/secure/replacement/<name>/`
to the (remediated) node and restart its agent. The node reconnects
using the new cert, whose `NotBefore` equals the watermark and
therefore passes the revocation check. There is no separate
"unblock" step — the watermark entry remains in place indefinitely
(it is tiny and append-only) and simply does not affect the fresh
cert.

If the pool CA itself is compromised, CA rotation is required (see
Rotation below). See the [Security Design][security] for detailed
key compromise response procedures.

#### Rotation

- **Node cert renewal**:
  `dmg pool add-client POOL --pool-ca-key /path/to/pool_ca.key --node node1 --output ...`
  Replace cert on the node. No pool property change.
- **Pool CA rotation (scheduled)**: When the pool CA key is *not*
  believed compromised — expiry rollover, key-material hygiene, moving
  to a stronger algorithm — the CA bundle's overlap support means the
  rotation is a rolling migration rather than a flag-day cutover:
  1. Generate a new pool CA (appended to the bundle):
     `dmg pool set-cert POOL --daos-ca-key /path/to/daosCA.key --output ...`
  2. Re-issue node certs signed by the new CA:
     `dmg pool add-client POOL --pool-ca-key /path/to/new_ca.key --node ... --output ...`
  3. Distribute new certs to all nodes (site-specific tooling).
  4. Once all nodes have new certs, remove the old CA:
     `dmg pool delete-cert POOL --fingerprint <old_ca_fingerprint>`

#### Rotation (emergency, pool CA key compromise)

The scheduled-rotation procedure above is *wrong* if the pool CA
private key itself is believed compromised. Its whole point is to keep
legitimate clients connected throughout — but that also means any cert
an attacker can mint with the stolen key continues to be accepted
until the old CA is finally removed from the bundle (step 4).

When the pool CA key is compromised, the priority inverts: stop
accepting certs signed by the compromised key as fast as possible,
even if that means a service interruption while the replacement is
rolled out. The node certs issued by `add-client` and `revoke-client`
are only "dead" once their issuing CA is out of the bundle; per-CN
watermarks cannot help here because the attacker can forge a cert for
any previously-unseen CN.

Procedure:

1. **Drop the compromised CA first.** Every legitimate connect begins
   failing at chain validation at this moment; this is intentional and
   is the only instant at which the attacker's forged certs also stop
   working:
   ```
   dmg pool delete-cert POOL --fingerprint <compromised_ca_fingerprint>
   ```
   If the compromised CA was the only CA in the bundle, the pool now
   rejects every POOL_CONNECT with `-DER_NO_CERT` until step 3. If a
   clean CA was already in the bundle (e.g., because a scheduled
   rotation had begun), its nodes keep working; skip to step 3 for
   the nodes that had the old cert.
2. **Generate a fresh pool CA.** Use a freshly-generated DAOS CA key
   or any key that is definitely not derived from the compromised one:
   ```
   dmg pool set-cert POOL --daos-ca-key /path/to/daosCA.key --output /secure/new
   ```
3. **Re-issue node certs and redistribute.** Every legitimate node
   needs a new cert signed by the new CA:
   ```
   dmg pool add-client POOL --pool-ca-key /secure/new/<pool_uuid>_ca.key \
       --node node1 --node node2 ... --output /secure/new/certs
   ```
   Distribute and restart agents. Service is restored node-by-node as
   each node picks up its new cert.
4. **Evict existing handles if warranted.** Nothing in steps 1-3 drops
   an already-open pool handle; an attacker holding a handle via an
   old session continues to hold it. If active misuse is suspected,
   run `dmg pool evict POOL` after step 1 (or combine it with a
   `revoke-client --evict-all-handles` on any CNs believed abused).

The scheduled and emergency procedures differ precisely in whether
"keep service up" or "close the breach" wins during the overlap
window. Scheduled: keep service up, remove old CA last. Emergency:
close the breach, accept downtime until replacements are rolled out.

Certificate distribution is an out-of-band administrative task
handled by site deployment tooling (Ansible, appliance framework,
etc.). DAOS provides the CA bundle overlap window so that
distribution does not need to be atomic.

## Potential Future Work

- **System-level node authentication**: A system-wide client CA for
  per-node identity and revocability without per-pool configuration.
- **Credential signing unification**: Using node cert keys to sign
  AUTH_SYS credentials, collapsing credential verification and node
  auth into a single signature.

See the [Security Design][security] for analysis of both directions.

- Telemetry counters for node cert validation (total, pass, fail by
  reason).
- CRL support for individual node certificate revocation.
- Multiple accepted CAs per pool for CA rotation windows.
- Certificate-bound pool ACL entries (node cert CN as ACL principal).
- Integration with external PKI / certificate management systems.
- Per-container certificates (Kubernetes/container workloads).
- Appliance framework integration for automated provisioning.
- Certificate expiration monitoring (RAS events / warnings before
  expiry).

[multiauth]: https://daosio.atlassian.net/wiki/spaces/DC/pages/12796362760/Multiple+Authentication+Types
[security]: node_authentication_security.md
