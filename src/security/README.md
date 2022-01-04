# Data Plane Security Module

The DAOS security module centralizes all access and security-related
functionality in the DAOS data plane in a single module.

The functionality in this module includes:

- Client library requests to the DAOS Agent for a credential for authentication.
- Server requests to the Control Plane to validate a signed client credential.
- Generating default Access Control Lists for pools and containers.
- Deriving pool and container capabilities from the combination of the Access
  Control List and the client credential.
- Access control checks for pool and container operations.

## Credential Generation and Validation

Details on the credential generation and validation process
are outlined
[in the Control Plane documentation](/src/control/security/README.md).

## Access Control Internals

This section covers the implementation details of pool and container access
control.

See the [security overview](/docs/overview/security.md#access-control-lists) for
background on DAOS Access Control Lists.

See the [Admin Guide](/docs/admin/pool_operations.md#access-control-lists)
for a higher-level view of pool access control.

See the [User Guide](/docs/user/container.md#access-control-lists)
for a higher-level view of container access control.

### Pool Access

When a client connects to a pool, the user credential that was used to make the
connection is used, in combination with the pool ACL and the requested access
type (RO or RW), to generate an internal set of security capabilities for the
pool handle. These capabilities map to the valid permissions set for pools and
are used for future access decisions for that pool handle, for pool-level
operations such as container creation.

These capabilities persist for the life of the handle, even if someone modifies
the pool ACL or ownership. The modifications won't be used for access decisions
until the user attempts to get a new handle for the pool. The pool handle cannot
be revoked.

In addition, the validated credential is saved in the handle data in the DAOS
data plane during pool connect, and remains associated with the pool handle
for its lifetime.
This original credential is used for access decisions when the user
interacts with existing containers. It is used both when the user attempts to
open a container (acquire a container handle) and when the user attempts to
delete a container, for which they may not be holding a handle.

### Container Access

The client process to open a container is similar to that for pool connection.
A set of security capabilities is determined for the container handle based on
the combination of the container ACL and the credential associated with the pool
handle, along with the type of access requested (RO or RW). The security
capabilities calculated at the time of the container open are used for container
access decisions throughout the lifetime of the container handle. The container
handle cannot be revoked.

### Container Destroy

Container deletion is a special operation in the context of access control.
There are two levels of permission that may have been granted.

At the pool level, a user may have been granted the administrator-like privilege
of deleting any container, even those that they have no access to. This is the
first and fastest check: this permission is included in the pool handle's
security capabilities.

If the user does not have the pool level delete privilege, we must determine the
security capabilities of the pool handle's credential on the container before we
know if the holder of the handle is permitted to delete it. This is the same
process used during container open, wherein the credential and container ACL are
used together. If the user has permission via the container ACL to delete that
specific container, the operation is allowed to proceed.
