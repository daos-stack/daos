# Node Authentication Prototype Validation Guide

This guide walks through exercising the node authentication prototype
on a running DAOS system. It covers certificate management, certificate
revocation via per-CN watermarks, CA bundle rotation, and the
cryptographic behaviour of revocation under exfiltration.

## Prerequisites

- A running DAOS system with at least one server and two client nodes
- `dmg` configured with admin access
- A pool to test with (the guide uses `POOL` as a placeholder)
- A DAOS CA whose private key is reachable from the admin host where
  `dmg` runs (see below)

### Create the DAOS CA

The pool CA management commands (`pool set-cert`, `pool revoke-client`)
need the DAOS CA private key in order to sign the per-pool intermediate
CA. On a system that already has DAOS deployed normally, this key
exists at `/etc/daos/certs/daosCA.key`. If you're starting from scratch
on a test system without a DAOS CA, generate one with
`gen_certificates.sh`:

```bash
# On the admin host. [DIR] defaults to the current directory.
sudo /usr/lib64/daos/certgen/gen_certificates.sh /etc/daos/

# Result:
#   /etc/daos/daosCA/daosCA.crt          # DAOS CA cert
#   /etc/daos/daosCA/private/daosCA.key  # DAOS CA private key
#   /etc/daos/daosCA/certs/{agent,server,admin}.{crt,key}
```

`gen_certificates.sh` also produces the agent/server/admin leaf certs
that the standard DAOS install would otherwise have set up. If your
servers and agents are already running, leave the existing leaf certs
alone — the only piece this guide needs is the CA cert+key.

The DAOS CA private key (`daosCA.key`) is what the rest of this guide
passes to `--daos-ca-key`. It must be readable by the user running
`dmg`. In a production deployment the key is typically held offline or
on a secured admin host; for the prototype walkthrough you can either
copy it locally or run `dmg` with sufficient privilege to read
`/etc/daos/daosCA/private/daosCA.key`.

For brevity, the rest of this guide writes the path as
`/etc/daos/certs/daosCA.key` as a generic placeholder. Substitute the
actual location from your install — typically
`/etc/daos/daosCA/private/daosCA.key` immediately after running
`gen_certificates.sh`, or wherever your site tooling stages the key
on admin hosts.

## 1. Basic Node Auth: Certificate Setup

### Generate a pool CA and set it on the pool

```bash
dmg pool set-cert POOL \
    --daos-ca-key /etc/daos/certs/daosCA.key \
    --output /tmp/pool_keys
```

This generates a pool-specific intermediate CA key pair, signs it with
the DAOS CA, and stores the public cert as a pool property. Verify:

```bash
dmg pool get-cert POOL
```

You should see the pool CA certificate details (subject, issuer,
fingerprint, expiration).

### Generate node certificates

```bash
dmg pool add-client POOL \
    --pool-ca-key /tmp/pool_keys/<pool_uuid>_ca.key \
    --node client1 --node client2 \
    --output /tmp/node_certs
```

This creates per-node cert/key pairs with `CN=node:<hostname>`.

### Generate tenant certificates (alternative)

```bash
dmg pool add-client POOL \
    --pool-ca-key /tmp/pool_keys/<pool_uuid>_ca.key \
    --tenant teamA \
    --output /tmp/tenant_certs
```

This creates a cert with `CN=tenant:teamA`. Deploy the same cert+key
to all nodes belonging to the tenant. `--node` and `--tenant` are
mutually exclusive.

### Distribute certificates to nodes

Copy each node's cert and key to its `node_cert_dir`:

```bash
# On admin node:
scp /tmp/node_certs/client1/* client1:/etc/daos/certs/daos_server/node_certs/
scp /tmp/node_certs/client2/* client2:/etc/daos/certs/daos_server/node_certs/
```

Restart agents on both nodes to pick up the new certs:

```bash
# On each client:
systemctl restart daos_agent
```

### Verify pool connect works

```bash
# On client1 (should succeed):
daos pool query POOL
```

### Verify unauthenticated connect fails

On a client node that does NOT have a cert for this pool:

```bash
# On client3 (no cert -- should fail with DER_NO_CERT):
daos pool query POOL
```

Expected error: `-DER_NO_CERT`

### Cross-pool isolation

Create a second pool with its own CA. Verify that a node cert for
POOL can't be used to connect to the second pool:

```bash
dmg pool create -z 1G --label POOL2
dmg pool set-cert POOL2 \
    --daos-ca-key /etc/daos/certs/daosCA.key \
    --output /tmp/pool2_keys

# client1 has a cert for POOL but NOT for POOL2:
# On client1 (should fail with DER_NO_CERT -- cert for wrong pool):
daos pool query POOL2

# Generate a cert for POOL2 and distribute:
dmg pool add-client POOL2 \
    --pool-ca-key /tmp/pool2_keys/<pool2_uuid>_ca.key \
    --node client1 --output /tmp/pool2_certs
scp /tmp/pool2_certs/client1/* client1:/etc/daos/certs/daos_server/node_certs/
# Restart agent on client1

# Now it should work:
daos pool query POOL2
```

### Cert ignored on non-auth pool

A node with certs can still connect to pools that don't require them.
The cert is silently ignored:

```bash
dmg pool create -z 1G --label POOL_OPEN
# No set-cert -- this pool has no CA requirement

# On client1 (has certs for other pools, but POOL_OPEN doesn't care):
daos pool query POOL_OPEN
# Expected: success (cert is ignored, normal AUTH_SYS flow)

dmg pool destroy POOL_OPEN
```

## 2. Certificate Revocation

Revocation uses a per-CN watermark mechanism: every cert carries a
`NotBefore` timestamp, and the pool keeps a map of `CN → watermark`.
On pool connect, any cert whose `NotBefore` is strictly less than the
watermark for its CN is rejected. `dmg pool revoke-client` atomically
issues a fresh replacement cert (with `NotBefore == new watermark`)
and advances the watermark, so the old cert dies and the new one
survives in a single operation.

Revocation only applies to pools with node authentication enabled —
a pool without `DAOS_PROP_PO_POOL_CA` has nothing to revoke.

### Revoke a node

```bash
dmg pool revoke-client POOL \
    --pool-ca-key /tmp/pool_keys/<pool_uuid>_ca.key \
    --node client1 \
    --output /tmp/revoked_certs
```

This generates a fresh keypair and cert for `node:client1`, advances
the watermark for that CN to the new cert's `NotBefore`, and writes
the replacement files to `/tmp/revoked_certs/client1/`. The
previously-issued cert for `client1` is now dead.

### Verify the old cert is rejected

Before distributing the replacement, `client1` still has its old cert
on disk. Any pool connect attempt with the old cert should fail:

```bash
# On client1 (still has the old cert):
daos pool query POOL
# Expected: DER_BAD_CERT (cert NotBefore is below the new watermark)
```

Check the control-plane log on the server:

```bash
grep "has been revoked" /var/log/daos_server.log
# Expected: cert for "node:client1" has been revoked
#           (NotBefore=... < watermark=...)
```

### Distribute the replacement and verify it works

```bash
# On admin node:
scp /tmp/revoked_certs/client1/* client1:/etc/daos/certs/daos_server/node_certs/

# On client1:
systemctl restart daos_agent
daos pool query POOL
# Expected: success (new cert's NotBefore matches the watermark)
```

### Revoke a tenant

```bash
dmg pool revoke-client POOL \
    --pool-ca-key /tmp/pool_keys/<pool_uuid>_ca.key \
    --tenant teamA \
    --output /tmp/revoked_certs
```

This advances the watermark for `tenant:teamA` and writes the new
tenant cert+key. Every node sharing the tenant cert must be
redistributed the replacement — the revocation kills the shared
cert for *all* tenant members simultaneously. This is the intrinsic
cost of a shared identity; the watermark does not make it worse, but
it does not make it better either.

### Verify other clients still work

```bash
# On client2 (not revoked):
daos pool query POOL
# Expected: success
```

### List revocations

```bash
dmg pool list-revocations POOL
# Expected output:
#   node:client1    2026-04-15T14:00:29Z
#   tenant:teamA    2026-04-15T14:01:02Z
```

### Watermark monotonicity

Revoking the same CN twice in close succession demonstrates that the
watermark advances strictly: the second revocation produces a new cert
with `NotBefore` greater than the first.

```bash
dmg pool revoke-client POOL --pool-ca-key ... --node client2 --output /tmp/r1
dmg pool list-revocations POOL  # watermark W1 recorded

dmg pool revoke-client POOL --pool-ca-key ... --node client2 --output /tmp/r2
dmg pool list-revocations POOL  # watermark W2 > W1

# The cert in /tmp/r1/client2/ is now dead.
# Only the cert in /tmp/r2/client2/ is valid.
```

The monotonicity invariant ("a revocation must never lower a
previously-committed watermark") is enforced by the control library on
behalf of `revoke-client`; consult the main design doc for the
rationale.

## 3. Pool-Wide Handle Eviction on Revocation

`revoke-client` does *not* evict active handles by default. Node
certificate validation only runs on initial `POOL_CONNECT`, so an
attacker who holds a live handle retains access until the handle is
torn down independently. For incident response, `--evict-all-handles`
pairs revocation with a pool-wide `dmg pool evict`:

```bash
# On client1, hold an active handle:
daos cont create POOL --label testcont --type POSIX
# Leave a mount or open handle active

# On admin node:
dmg pool revoke-client POOL \
    --pool-ca-key /tmp/pool_keys/<pool_uuid>_ca.key \
    --node client1 \
    --output /tmp/revoked_certs \
    --evict-all-handles

# On client1, operations using the old handle should fail (handle
# evicted). Reconnect attempts with the old cert hit the revocation
# watermark:
daos cont query POOL testcont
# Expected: connection failure
```

After distributing the replacement cert and restarting the agent,
`client1` can reconnect; the new cert passes the watermark check.
The eviction is pool-wide (all handles), not node-targeted — legitimate
clients must transparently reconnect, which succeeds because their
certs are still valid. This is the recommended incident-response
sequence when there is any suspicion that an attacker may hold live
handles.

## 4. CA Bundle Rotation

This verifies that you can rotate pool CAs without a flag-day outage.

### Add a second CA to the bundle

```bash
dmg pool set-cert POOL \
    --daos-ca-key /etc/daos/certs/daosCA.key \
    --output /tmp/pool_keys_v2
```

### Verify both old and new certs are accepted

```bash
dmg pool get-cert POOL
# Should show two CA certificates [0] and [1]

# client1 still has certs signed by old CA -- should still work:
daos pool query POOL
```

### Re-issue node certs with the new CA

```bash
dmg pool add-client POOL \
    --pool-ca-key /tmp/pool_keys_v2/<pool_uuid>_ca.key \
    --node client1 --node client2 \
    --output /tmp/node_certs_v2

# Distribute and restart agents
scp /tmp/node_certs_v2/client1/* client1:/etc/daos/certs/daos_server/node_certs/
scp /tmp/node_certs_v2/client2/* client2:/etc/daos/certs/daos_server/node_certs/
# Restart agents on both nodes
```

### Remove the old CA

```bash
# Get fingerprint of the old CA:
dmg pool get-cert POOL
# Note the fingerprint of [0] (the original CA)

dmg pool delete-cert POOL --fingerprint <old_ca_fingerprint>

dmg pool get-cert POOL
# Should show only one CA certificate now
```

### Verify old certs are now rejected

If any node still has certs signed by the old CA, it should fail:

```bash
# Any node with old cert: should fail with DER_BAD_CERT
# (All nodes should have been re-issued above, so this requires
# intentionally skipping one node's cert update to test)
```

## 5. Agent CN Validation

The agent validates the CN prefix on every cert load. Node-scoped
certs (`node:`) must match the local hostname. Tenant-scoped certs
(`tenant:`) skip the hostname check. Certs with no recognized prefix
are rejected.

### 5a. Node cert on wrong host

```bash
# Copy client2's cert to client1:
scp client2:/etc/daos/certs/daos_server/node_certs/<pool_uuid>.crt \
    client1:/etc/daos/certs/daos_server/node_certs/<pool_uuid>.crt
scp client2:/etc/daos/certs/daos_server/node_certs/<pool_uuid>.key \
    client1:/etc/daos/certs/daos_server/node_certs/<pool_uuid>.key

# Restart agent on client1 to clear cert cache
systemctl restart daos_agent

# On client1 (cert CN is "node:client2", hostname is "client1"):
daos pool query POOL
# Expected: DER_NO_CERT (agent refuses to use mismatched cert)
```

Check the agent log:

```bash
grep "does not match hostname" /tmp/daos_agent.log
# Expected: node cert CN "node:client2" does not match hostname "client1"
```

### 5b. Tenant cert on any host

```bash
# Deploy a tenant cert to client1:
scp /tmp/tenant_certs/teamA/<pool_uuid>.crt \
    client1:/etc/daos/certs/daos_server/node_certs/
scp /tmp/tenant_certs/teamA/<pool_uuid>.key \
    client1:/etc/daos/certs/daos_server/node_certs/
systemctl restart daos_agent

# On client1 (cert CN is "tenant:teamA" -- no hostname check):
daos pool query POOL
# Expected: success (tenant certs skip hostname validation)
```

### Restore the correct cert

```bash
# Re-distribute client1's own cert and restart the agent
```

## 6. Revocation Under Exfiltration

This exercise demonstrates that `revoke-client` cryptographically
invalidates an exfiltrated cert, without relying on the agent's
hostname check as a security boundary. The agent's CN check
(`gethostname()` vs. cert CN suffix) still runs on the legitimate
node, but it is defense-in-depth against misdeployment, not an
attacker defense — an attacker who controls the client machine can
trivially bypass it, so the revocation mechanism must be effective
independent of it.

### Setup

```bash
# Assume client1 has been compromised and its cert+key exfiltrated.
# Copy them to an attacker-controlled machine (or simulate by copying
# to a second host under your control).
```

### Scenario A: Attacker keeps the exfiltrated cert unchanged

On the attacker machine, the stock agent's CN check will reject the
cert because the CN suffix (`client1`) does not match the local
hostname (`attacker-box`). The attacker side-steps this by running a
modified agent (or a purpose-built client) that skips the CN check
and speaks the dRPC/POOL_CONNECT wire format directly. Nothing on
the server side can distinguish this from a legitimate connect
attempt — same cert, same valid PoP (attacker has the private key),
same credential machine name ("client1") that the attacker sets in
the AUTH_SYS credential. The only server-side defense is the
watermark.

### Scenario B: Admin revokes client1

```bash
dmg pool revoke-client POOL \
    --pool-ca-key /tmp/pool_keys/<pool_uuid>_ca.key \
    --node client1 \
    --output /tmp/revoked_certs \
    --evict-all-handles
```

The attacker's exfiltrated cert has `NotBefore` earlier than the
newly-committed watermark. Any future connect attempt from the
attacker — regardless of hostname spoofing, modified agent, or
fabricated AUTH_SYS credentials — fails with `DER_BAD_CERT`. Any
pre-existing handles the attacker already held were evicted by
`--evict-all-handles`; reconnect attempts hit the revoked cert and
also fail.

The attacker's only escape is to obtain a fresh cert, which requires
the pool CA private key. That key is not on `client1` — it lives on
the admin host where `revoke-client` runs. An exfiltrated node cert
cannot forge its own replacement.

### Scenario C: Legitimate client1 remediated

After the compromise is contained and the node has been remediated,
distribute the replacement cert that `revoke-client` produced:

```bash
# On admin node:
scp /tmp/revoked_certs/client1/* client1:/etc/daos/certs/daos_server/node_certs/

# On client1:
systemctl restart daos_agent
daos pool query POOL
# Expected: success — new cert's NotBefore equals the watermark.
```

There is no separate "unblock" step. The watermark entry for
`node:client1` remains in the pool property forever (small,
append-only), but it no longer affects legitimate connects because
the in-use cert is the one `revoke-client` just issued.

### Scenario D: Tenant cert exfiltration

Tenant certs share a single identity across many machines, so a
single exfiltration leaks access from the whole tenant pool. The
revocation mechanism still works — `revoke-client --tenant` advances
the watermark for `tenant:<name>` — but the replacement must be
redistributed to every node belonging to the tenant before any of
them can reconnect. This is the intrinsic cost of a shared credential
and applies equally to any shared-secret scheme.

```bash
dmg pool revoke-client POOL \
    --pool-ca-key /tmp/pool_keys/<pool_uuid>_ca.key \
    --tenant teamA \
    --output /tmp/revoked_certs \
    --evict-all-handles

# Redistribute /tmp/revoked_certs/teamA/* to every tenant member
# and restart agents.
```

## Cleanup

```bash
# Remove node auth from the pool (clears the pool CA property):
dmg pool delete-cert POOL --all

# Verify pool works without certs:
daos pool query POOL
```

The `DAOS_PROP_PO_CERT_WATERMARKS` entries persist as dormant data on
the pool after node auth is removed. They are inexpensive (one
`CN → timestamp` entry per historical revocation) and will be
consulted again if node auth is re-enabled, so leaving them in place
is intentional.

## Troubleshooting

| Symptom | Likely cause |
|---------|-------------|
| `DER_NO_CERT` | Pool requires cert but node doesn't have one, agent rejected cert due to CN mismatch (node-scoped), or cert CN has no recognized prefix |
| `DER_BAD_CERT` | Cert not signed by any CA in the pool's bundle, cert expired, CN/machine name cross-validation mismatch, or cert has been revoked (`NotBefore` below the watermark for its CN) |
| Agent log: "does not match hostname" | Cert CN doesn't match `gethostname()` — cert is for a different node, or was misdeployed |
| Server log: "has been revoked" | Cert's `NotBefore` is less than the watermark for its CN; run `dmg pool revoke-client` to issue a fresh replacement, or redistribute the replacement from a prior revocation |
| `dmg pool get-cert` shows no CAs | Pool CA property is empty; node auth is not enabled |
| Old cert still works after CA rotation | Old CA hasn't been removed from the bundle yet |
| Revoked cert still accepted | Watermark write failed; check `dmg pool list-revocations` to confirm the entry was committed |
