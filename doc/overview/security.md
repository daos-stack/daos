<a id="4.4"></a>
# Security Model

DAOS uses a flexible security model that seperates authentication from
authorization. It is designed to have a minimal impact on the I/O path.

There are two areas of DAOS that require access control. At the user level,
clients must be able to read and modify only pools and containers to which they
have been granted access. At the system and administrative levels, only
authorized components must be able to access the DAOS management network.

<a id="4.4.1"></a>
## Authentication

There are different means of authentication depending on whether the caller is
accessing client resources or the DAOS management network.

### Client Library

The client library is an untrusted component. A trusted process, the DAOS agent,
runs on the client node and authenticates the user process.

The DAOS security model is designed to support different authentication methods
for client processes. Currently, we support AUTH_SYS authentication only.

### DAOS Management Network

Each trusted DAOS component (agent, server, and administrative tool) is
authenticated by means of a certificate generated for that component. These
components identify one another over the DAOS management network via
mutually-authenticated TLS.

<a id="4.4.2"></a>
## Authorization

Client authorization for resources is controlled by the Access Control List
(ACL) on the resource, while authorization on the management network is
achieved by settings on the
[certificates](/doc/admin/deployment.md#certificate-configuration)
generated while setting up the DAOS system.

### Access Control Lists

Client access to resources like pools and containers is controlled by
[DAOS Access Control Lists](/doc/user/acl.md). These Access Control Lists are
derived in part from NFSv4 ACLs, and adapted for the unique needs of a
distributed system.

The client may request read-only or read-write access to the resource. If the
resource ACL doesn't grant them the requested access level, they won't
be able to connect. While connected, their handle to that resource grants their
permissions for specific actions.

The permissions of a handle last for the duration of its existence, similar to
an open file descriptor in a POSIX system. A handle cannot currently be revoked.

### Component Certificates

Access to DAOS management RPCs is controlled via the CommonName (CN) set in
each management component certificate. A given management RPC may only be
invoked by a component which connects with the correct certificate.
