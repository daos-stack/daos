# Per-Pool Node Authentication

DAOS user authentication establishes *who* is connecting to a pool: the
agent verifies the local process's identity and signs a credential that
the servers trust. It does not establish *where* the connection comes
from — any machine running a trusted agent can present credentials for
any pool.

Per-pool node authentication adds that missing dimension. When enabled
on a pool, a client must first present a certificate proving that its
machine (or tenant group) has been explicitly granted access to that
pool. If the client fails to prove that it is even allowed to attempt
to connect, then its request is rejected immediately, regardless of
the connecting user's ACL permissions.

Typical uses:

- **Multi-tenant clusters**: restrict each tenant's pools to that
  tenant's compute nodes.
- **Sensitive pools**: limit access to a small set of hardened nodes,
  even though the whole cluster shares one DAOS system.

## How It Works

Each protected pool has its own intermediate **pool CA**, signed by the
DAOS CA, and stored as a property on the pool. The administrator then
uses the pool CA to issue certificates to clients:

- A **node certificate** (`node:<hostname>`) grants access to exactly
  one client machine. The server verifies that the certificate's name
  matches the connecting client's machine name.
- A **tenant certificate** (`tenant:<name>`) is shared by a group of
  machines. Any node holding the tenant certificate and key may
  connect.

At pool connect time, the agent attaches the certificate and a signed,
time-limited proof-of-possession to the connection request. The server
validates the certificate chain against the pool CA, checks the name
and revocation state, and verifies the proof-of-possession before
allowing the connection. Pools without a pool CA are unaffected — the
connect path is unchanged.

All node authentication administration lives under one command group:

```
dmg pool node-auth enable      Enable node authentication on a pool
dmg pool node-auth generate-ca Generate a pool CA without contacting the system
dmg pool node-auth disable     Disable node authentication on a pool
dmg pool node-auth status      Show CAs and revocations for a pool
dmg pool node-auth issue       Issue node/tenant certificates
dmg pool node-auth generate-cert Mint node/tenant certificates without contacting the system
dmg pool node-auth revoke      Revoke a node or tenant identity
dmg pool node-auth add-ca      Add a CA to the pool's bundle (rotation)
dmg pool node-auth remove-ca   Remove a CA from the pool's bundle
```

## Requirements

- Servers and clients at a DAOS version supporting pool protocol v8.
  Older servers ignore the feature entirely; older clients cannot
  connect to a pool that requires node authentication (the connection
  fails with `-DER_PROTO`).
- Transport security enabled (`allow_insecure: false` on servers,
  agents, and dmg). This implies that the DAOS CA public certificate
  (`daosCA.crt`) has been distributed to servers and agents, as it is
  required to validate certificate chains.
- Access to the DAOS CA private key (`daosCA.key`) only when generating
  pool CAs. By default, dmg looks for it beside the DAOS CA certificate,
  but the location is overridable if the private key is stored elsewhere.

Key custody by operation: generating a pool CA (`enable`/`add-ca`)
signs with the **DAOS CA key**; issuing client certificates (`issue`)
signs with the **pool CA key**; revoking needs *no* key at all — see
[Revoking Access](#revoking-access).

## Enabling Node Authentication on a Pool

Generate a pool CA, signed by the DAOS CA, and install it on the pool:

```console
$ dmg pool node-auth enable tank
Pool CA written to /etc/daos/certs/pools/<pool_uuid>_ca.crt and
  /etc/daos/certs/pools/<pool_uuid>_ca.key
Node authentication enabled on tank
Handles evicted: 0
Pool CA valid until 2029-06-14; rotate before then (add-ca, reissue, remove-ca)
```

The pool CA is valid until the DAOS CA itself expires unless
`--validity` asks for less (`90d`, `26w`, `2y`); it can never outlast the DAOS CA.
See [Certificate Lifetimes and Renewal](#certificate-lifetimes-and-renewal).

The pool CA is signed with the DAOS CA key from its default location
(`--daos-ca-key` overrides). The CA *certificate* is stored on the
pool; the CA *private key* is written only on the admin node, to
`pools/` under the directory that holds dmg's own certificates
(`transport_config.key` in `dmg.yml`, default `/etc/daos/certs`).
`issue` looks for it there. Pass `--output` to keep it somewhere else.
See [Protecting the Pool CA Key](#protecting-the-pool-ca-key).

If the DAOS CA key is kept on a host without a connection to the
system (an air-gapped signing host), generate the pool CA there and
import only the certificate:

```console
# on the signing host, which holds daosCA.key and does NOT require access to
# the running system:
$ dmg pool node-auth generate-ca <pool_uuid> --daos-ca-cert /path/to/daosCA.crt \
    --daos-ca-key /path/to/daosCA.key -o /path/to/pool_cas
Pool CA written to /path/to/pool_cas/<pool_uuid>_ca.crt and
  /path/to/pool_cas/<pool_uuid>_ca.key
Pool CA valid until 2029-06-14
Install it with: dmg pool node-auth enable <pool_uuid> --cert /path/to/pool_cas/<pool_uuid>_ca.crt

# on the normally-connected admin node:
$ dmg pool node-auth enable tank --cert /path/to/pool_cas/<pool_uuid>_ca.crt
```

`generate-ca` takes the pool UUID (`dmg pool query tank` shows it),
since it cannot resolve a label without the system. The private key
stays on the signing host; mint node certificates there too, with
`generate-cert` — see [Issuing Client Certificates on a signing
host](#issuing-client-certificates-on-a-signing-host).

A pool CA is bound to one pool, and the server refuses to install a CA
generated for a different pool, as well as any CA that does not chain
to the DAOS CA.

From the moment authentication is enabled, **every connection to the
pool requires a certificate**: existing handles are evicted, and
clients reconnect once their certificates are deployed. Pass
`--no-evict` to keep existing handles open until they close on their
own.

Inspect a pool's node authentication state at any time:

```console
$ dmg pool node-auth status tank
Node authentication: enabled
CA Certificate [0]:
  Subject:     ...
  Issuer:      ...
  Not Before:  ...
  Not After:   ...
  Fingerprint: 3f2a...
Revocations: none
```

## Issuing Client Certificates on an admin node

`dmg pool node-auth issue` issues client certificates signed with the
pool CA key on the admin node. These certificates must be securely
distributed to the client nodes that will use them. In this mode, dmg
contacts the management service to resolve the pool and perform revocation
watermark bookkeeping, if necessary.

Per-node certificates take the client's *short* hostname (`client01`,
not `client01.example.com`):

```console
$ dmg pool node-auth issue tank --node client01 --node client02
  node:client01: /etc/daos/certs/pools/<pool_uuid>/client01/<pool_uuid>.crt, .../<pool_uuid>.key (valid until 2027-08-26)
  node:client02: /etc/daos/certs/pools/<pool_uuid>/client02/<pool_uuid>.crt, .../<pool_uuid>.key (valid until 2027-08-26)
Certificates issued for 2 node(s)
Deploy each pair to its node in /etc/daos/certs/node_certs/ (readable by the daos_agent user; key mode 0400)
```

A tenant certificate is shared by a group of machines:

```console
$ dmg pool node-auth issue tank --tenant teamA
```

Client certificates are valid for one year (`--validity 90d`, `26w`,
`2y` overrides) and never past the pool CA's expiry. `issue` reads the
pool CA key from its default location and stages the results under
`pools/<pool_uuid>/<name>/` beside it; `--pool-ca-key` and `--output`
override. `--node` and `--tenant` cannot be mixed; names are limited to
alphanumerics, `.`, `-`, and `_`.

### Issuing Client Certificates on a signing host

When the pool CA key is kept on a host without a system connection,
`generate-cert` mints the same certificates there:

```console
# on the signing host, which holds <pool_uuid>_ca.key
$ dmg pool node-auth generate-cert <pool_uuid> --pool-ca-key /path/to/pool_cas/<pool_uuid>_ca.key \
    --node client01 --node client02 -o /path/to/node_certs
  node:client01: /path/to/node_certs/client01/<pool_uuid>.crt, .../<pool_uuid>.key (valid until 2027-08-26)
  node:client02: /path/to/node_certs/client02/<pool_uuid>.crt, .../<pool_uuid>.key (valid until 2027-08-26)
Certificates issued for 2 node(s)
Deploy each pair to its node in /etc/daos/certs/node_certs/ (readable by the daos_agent user; key mode 0400)
Revocation state was not consulted: revoke before minting a replacement for a revoked identity
```

### Reissuing a certificate

Reissuing normally follows a revocation — see [Revoking
Access](#revoking-access): revoke the identity, then `issue` it a new
certificate, which is dated past the revocation and valid immediately.
`issue --replace` does both in one step.

Issuing without revoking does not invalidate the earlier certificate;
it stays usable until it expires. `issue` therefore refuses to
overwrite a node certificate it previously staged unless that identity
has since been revoked:

```console
$ dmg pool node-auth issue tank --node client01
ERROR: certificate already issued for node(s) client01 in
  /etc/daos/certs/pools/<pool_uuid>; reissuing leaves the existing
  certificate valid until it expires. Pass --replace to revoke it
  first, or --output to issue alongside it
```

`--replace` revokes the identity and then issues a certificate dated
past the revocation:

```console
$ dmg pool node-auth issue tank --node client01 --replace
Revoked the existing certificate(s) for node(s) client01
  node:client01: /etc/daos/certs/pools/<pool_uuid>/client01/...
```

Tenant certificates are held by many machines, so `issue` does not
refuse a staged copy and rejects `--replace` for `--tenant`. Roll the
new tenant certificate out to every holder, then revoke the old
identity.

### Choosing node vs. tenant certificates

| | Node certificate | Tenant certificate |
|---|---|---|
| Grants access to | one named machine | any machine holding it |
| Server checks machine name | yes | no |
| Deployment | unique file per node | same file on every node |
| Revocation blast radius | that node's handles | **all** pool handles (default) |

Prefer node certificates; use tenant certificates when per-node
issuance is impractical.

## Deploying Certificates to Clients

Copy each certificate and key to its client node, into the agent's
node certificate directory (default `/etc/daos/certs/node_certs/`),
named `<pool_uuid>.crt` and `<pool_uuid>.key`:

```console
# on client01
$ mkdir -p /etc/daos/certs/node_certs
$ scp admin:/etc/daos/certs/pools/<pool_uuid>/client01/<pool_uuid>.* \
      /etc/daos/certs/node_certs/
$ chown daos_agent:daos_agent /etc/daos/certs/node_certs/*
$ chmod 0400 /etc/daos/certs/node_certs/<pool_uuid>.key
```

The key must be readable by the `daos_agent` user and not writable by
group or others; distribute it over a secure channel. A node holds one
certificate per pool: its node certificate or a tenant certificate.
The agent picks up new or replaced files without a restart.

## Configuring Agents

By default, the agent will automatically check for a `<pool_uuid>.crt`
file under `node_certs/` in the configured certificate directory. The
only required configuration is for transport security to be enabled.

If the certificate file is missing or cannot be read by the agent,
then the client connection will proceed without the certificate and
proof-of-possession. If the pool has enabled node auth, the connection
will be rejected with an error.

Set `credential_config.node_cert_dir` in `daos_agent.yml` to use a
directory other than the default of `/etc/daos/certs/node_certs/`.

## Verifying a Deployment

After deploying a certificate, run the preflight check on the client
node. It verifies every link, from management-service reachability
through to the certificate chaining to the pool's *current* CA and the
identity not being revoked:

```console
$ daos_agent check-node-cert tank
  pool:                8f2e... (requires node certificates)
  cert dir:            /etc/daos/certs/node_certs (exists)
  cert file:           8f2e....crt  found, parses OK
  key file:            8f2e....key  found, mode 0400, owner daos_agent, matches cert
  CN:                  node:client01
  machine name:        client01  (match)
  validity:            2026-08-14 .. 2027-08-14 (OK)
  chain:               verifies against the pool's current CA
  revocation:          not revoked
```

Within 30 days of an expiry the check still passes but marks the line
`WARN`:

```console
  validity:            WARN 2026-08-14 .. 2026-09-10 (certificate expires in 15 day(s), on 2026-09-10 — reissue)
  pool CA:             WARN pool CA "DAOS Pool CA 8f2e..." expires in 15 day(s), on 2026-09-10 — rotate the pool CA (dmg pool node-auth add-ca)
```

For failures, see [Troubleshooting](#troubleshooting).

## Revoking Access

`dmg pool node-auth revoke` invalidates every certificate issued to an
identity — every copy, dated before the revocation — and evicts pool
handles. It needs pool admin privileges, not the pool CA key:

```console
$ dmg pool node-auth revoke tank --node client01
Revoked node:client01
  Watermark:       2026-08-13T17:22:05Z
  Handles evicted: 3 (machine)
  Revocation is by certificate identity: a host connecting with a tenant certificate reconnects; revoke the tenant to cut it off
```

Revocation is by identity, not by host:

- `--node client01` invalidates `node:client01` and evicts that
  machine's handles. A machine connected with a tenant certificate
  reconnects.
- `--tenant teamA` invalidates `tenant:teamA` and evicts **every
  handle on the pool**; clients holding other certificates reconnect.
- `--evict-all-handles` makes a node revocation evict pool-wide;
  `--no-evict` leaves existing handles alive (new connections with the
  revoked certificate are still refused).

To restore access, revoke first, then `issue` ([Reissuing a
certificate](#reissuing-a-certificate)). `Handles evicted: 0` on a
node revocation usually means a misspelled name — revoking an unknown
identity succeeds.

Revocations are listed by `node-auth status`:

```console
$ dmg pool node-auth status tank
...
Revocations:
  node:client01  2026-08-13T17:22:05Z
```

A revocation is retained until every CA in the pool's bundle postdates
it — that is, until the CA it applied to has been rotated out.

## Certificate Lifetimes and Renewal

An expired certificate is refused at connect like a revoked one:

| Certificate | Default lifetime | Override |
|---|---|---|
| Pool CA | Until the DAOS CA expires (`gen_certificates.sh` mints that for 3 years) | `--validity` on `enable`, `add-ca`, `generate-ca` |
| Client (node/tenant) | 1 year | `--validity` on `issue`, `generate-cert` |

`--validity` takes days, weeks, or years: `90d`, `26w`, `2y`. A
certificate never outlasts its signer: a pool CA is clipped to the DAOS
CA's expiry and a client certificate to the pool CA's. When the pool
CA expires, every new connection to the pool fails while existing
handles keep working; rotate the CA before that date.

Where the dates are visible:

- `enable`, `add-ca`, `generate-ca`, `issue`, and `generate-cert` print
  `valid until` for what they minted.
- `dmg pool node-auth status` lists `Not After` for each CA and adds a
  `WARNING` line within 30 days of expiry.
- `daos_agent check-node-cert <pool>` marks the `validity` line `WARN`
  within 30 days of the node certificate's expiry and adds a `pool CA`
  line within 30 days of any CA in the bundle.

Renewal:

- Client certificate: `issue --replace` (node) or `issue`, deploy, then
  `revoke` the old identity (tenant).
- Pool CA: [Rotating a Pool CA](#rotating-a-pool-ca), before its
  `Not After`.
- DAOS CA: outside this document; every pool CA signed by the old root
  must be rotated afterwards.

## Rotating a Pool CA

To rotate a pool's CA without interrupting clients:

1. `dmg pool node-auth add-ca tank` — install the new CA alongside the
   old one (`--cert` imports one from `generate-ca`). Both are now
   accepted. The new key pair replaces the old one in the pool CA
   directory, so `issue` signs with the new CA from here on; copy the
   old key first if you might abort the rotation.
2. Issue new client certificates and deploy them, replacing the old
   files.
3. `dmg pool node-auth status tank` — note the old CA's fingerprint.
4. `dmg pool node-auth remove-ca tank --fingerprint <old_fp>` —
   certificates issued by the old CA are no longer accepted.
   `remove-ca` refuses to remove the last CA; use `disable` to turn
   node authentication off.

## Disabling Node Authentication

```console
$ dmg pool node-auth disable tank
Node authentication disabled on tank
```

The pool reverts to standard authentication immediately. Deployed
client certificates can be deleted at leisure: the server ignores a
valid one, and the agent logs and skips one it cannot use.

## Configuration Reference

`daos_agent.yml`, under `credential_config`:

| Parameter | Default | Description |
|---|---|---|
| `node_cert_dir` | `/etc/daos/certs/node_certs/` | Directory searched for `<pool_uuid>.crt` / `<pool_uuid>.key`. |

`daos_server.yml`, under `transport_config`:

| Parameter | Default | Description |
|---|---|---|
| `pool_cert_max_clock_skew` | `5m` | Tolerance applied when checking proof-of-possession freshness and certificate NotBefore times. Raise (e.g. `300s`) for deployments with poor clock synchronization. Values require a unit suffix. |

## Protecting the Pool CA Key

- It is never stored on DAOS servers. `enable` or `generate-ca` writes
  it once, mode 0400, beside dmg's `admin.key`; give it the same
  protection, or use `--output` to place it under your PKI's key
  store.
- Only `issue` and `generate-cert` need it. Revocation, status, and
  `disable` do not; `enable` and `add-ca` sign with the DAOS CA key.
- Anyone holding it can mint certificates for any node or tenant name.
  If it is compromised, rotate the CA ([Rotating a Pool
  CA](#rotating-a-pool-ca)); revocation cannot help, since the holder
  can mint fresh, postdated certificates.
- If it is lost, existing clients keep working but no new certificates
  can be issued: `disable`, `enable` with a fresh CA, then reissue all
  client certificates.

## Troubleshooting

For any connection failure, first run `daos_agent check-node-cert
<pool>` on the affected client node.

Connection errors visible to the client:

| Error | Meaning | Check |
|---|---|---|
| `-DER_NO_NODE_CERT` | The pool requires a node certificate and this node presented none: none is deployed, or the agent found the deployed one unusable and left it out. | `check-node-cert` names the broken link; the agent log names the reason. If the node was never issued a certificate, `issue` one and deploy it. |
| `-DER_NO_CERT` | A certificate could not be read: the agent's own credentials, or the server's copy of the DAOS CA. Not specific to node authentication. | Paths and permissions under `transport_config`; the `daos_server` log names the file it could not load. |
| `-DER_BAD_CERT` | The server rejected the presented certificate: wrong CA, expired, revoked, or machine-name mismatch. | `check-node-cert`; if it passes, check for a rotated CA or a revocation (`node-auth status`) and the server log. |
| `-DER_NO_PERM` (with valid ACLs) | Proof-of-possession invalid or stale. | Clock synchronization between client and servers; consider `pool_cert_max_clock_skew`. |
| `-DER_PROTO` | Client too old to present certificates, but the pool requires them. | Upgrade the client, or `disable` node authentication on the pool. |

Server-side rejection details are logged on the control plane
(`daos_server` log, `node cert rejected (pool=..., status=...)`) and
are not returned to unauthenticated clients.
