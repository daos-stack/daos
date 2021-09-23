# Control Plane Security Package

The control plane security package provides a centralized source for Control
Plane functionality related to access control and identity. This functionality
is used throughout the control plane, in the DAOS Agent, DAOS Control Server,
and administrative tools.

This package implements the following:

- Load and validate DAOS certificates.
- Generate user credentials for client processes.
- Sign and validate a data token with a certificate.
- Configure gRPC communications to use mutually-authenticated TLS with
  certificates.
- Define access for gRPC commands by DAOS component certificate type.

## Credential Establishment

To assure the identity of a client process, the DAOS Agent generates a token to
assert the user's identity to the server, and signs this token using the agent
certificate. Credentials are established at the time of pool connect, and are
associated with the pool handle for its lifetime.

Currently, the only authentication method available in DAOS is AUTH_SYS. The
DAOS Agent gathers information about the effective UNIX user over the UNIX
Domain Socket connection used to communicate from the client library to the
DAOS Agent.

The workflow is as follows:

1.  A job application utilizing the DAOS client library requests to connect to
    a pool.
2.  The job application connects to the DAOS Agent requesting a security
    credential (token representing identity information).
3.  The DAOS Agent inspects the properties of the process (UID, GID, ACL,
    SELinux context) and uses that information to build and sign a credential.
4.  The DAOS Agent then passes this credential back to the DAOS client library,
    where it is used in the pool connect request, passing the security
    credential as an argument to the pool connect RPC.
5.  The DAOS I/O Engine receives the pool connect RPC and passes the security
    credential to the Control Plane component of the DAOS Server to validate the
    authenticity of the credential. If the credential is not valid, the request
    will be denied.
6.  If the security credential is deemed valid, the I/O Engine will compare the
    identity against the ACL for the pool, as described in the Access Control
    Enforcement section. If the identity has access, a pool handle is returned.
    Otherwise the error DER_NO_PERM is returned.

The framework used to establish the credentials can be further expanded to
handle additional properties and authentication methods.

## Certificate Usage in DAOS

Certificates are used in several locations to provide authentication and
validation of user identity in a DAOS system.
For information on how to set up the certificates in a DAOS system, see the
[Admin Guide](/docs/admin/deployment.md#certificate-configuration).

### Certificate Generation

When generating certificates for a DAOS system, four types of certificates are
generated.

The first to be generated is the CA root certificate specific to this DAOS
deployment. While it is advised to have per-system CA roots, there is no
mechanism preventing reuse between DAOS deployments. The CA root is used to
produce and validate the three remaining certificate types.

The three remaining certificate types that are generated are the admin, agent,
and server certificates. The admin certificate is used to protect the gRPC
channel between the administrative node and also provide validation of the
administrative user. The Agent certificate is used to ensure that the compute
node belongs to the DAOS system and also provides validation of process identity
credentials. The server certificate is used in three separate ways. First, it is
used to protect the gRPC channel between the administrative node and the server,
second, it protects the channel between DAOS servers, and finally, it provides
signing functionality for data transfers between the server and compute nodes.

DAOS provides a script and openssl configuration files for generating the
various certificates needed by a DAOS installation. The script will craft a CA
root certificate using the various configuration files found
[in the DAOS source tree](/utils/certs).
The config files contain the signing policies for the various other types of
certificates as well, ensuring the cryptography used is as directed by current
best practices for protecting TLS channels.

For the remaining certificates, DAOS uses the common name to ensure that the
correct certificates are used for the correct components. Initially, there will
only be one class of administrative user, and the Common Name for the gRPC TLS
channel will be admin, ensuring only admin certificates may be used to issue
administrative commands to the control plane. This is to ensure that if a key
and cert are obtained from a compute node that it cannot be used to connect to
the administrative interface. Likewise, we ensure that the appropriate certs are
used by the Agent and the Server by encoding their names into the Common Name.

### Protecting Administrative Channels with Certificates

Administration of a DAOS cluster will be performed by an administrator using the
dmg utility and interfacing with the control plane service for the cluster. The
control plane service is a distributed service provided by the DAOS cluster and
resides on each node acting as a storage node. Even though the control plane
service is present on every node, a subset of them is designated as the
management nodes, and if you attempt to connect to a non-management node, it
will attempt to redirect you to the entry point node for the cluster. The
connections between dmg and the control plane processes are performed using
gRPC. gRPC is an http2 based microservice architecture. The data structure and
service definitions for gRPC are written in protobuf and then translated into
the various languages used by DAOS (golang and C).

gRPC provides support for TLS authentication of the channel between the gRPC
client and server. It provides the ability to authenticate both ends of the
channel using both server and client-side validation. gRPC provides a mechanism
to interpose additional checks on the TLS handshake to ensure certain properties
of the connection are guaranteed. We use a server-side gRPC interceptor to
ensure that the certificate that was used to negotiate the channel contains the
appropriate Common Name.

### Host Authentication with Certificates

Every compute node in the cluster is assigned a certificate for its agent. The
agent certificate is used to verify the host's authenticity and the validity of
the data it is signing.  As part of the credential establishment process, an
identity token is bound with a verifier consisting of a signed hash of the
identity token along with the CommonName of the host signing the token. The
agent will use the CommonName in the certificate in the token verifier. When
the server receives the verifier, it will check for a certificate named with the
CommonName for validation.

Part of the setup of the DAOS cluster is that all nodes acting as a server must
have the certificate of all Agents that will be present in the cluster. In the
case of a single certificate shared by all agents in the cluster, which is
expected to be the most common case, this means that all servers will have a
single Agent certificate file. The requirement that agents must be known to
servers ahead of time makes it so an agent cannot be placed into the cluster
without a properly issued certificate.

### Credential Validation with Certificates

As mentioned in the previous sections, the Agents are responsible for generating
identity tokens and signing them. This action is performed when a client
application requests to connect to a pool. The Agent inspects the requesting
process and generates a hashed verifier token and signs it with the private key
file associated with the Agent's certificate. This token is packaged as part of
the pool connect operation and is sent to the data plane using an RPC over the
RDMA fabric channel. The data plane server sends a request to the control plane
server to validate the identity token that has been provided.
The control plane server inspects the verifier, extracts the name of the
providing host, and checks for a certificate with that name. The server then
validates the signature of the token using the certificate file associated with
that host. If the identity token validates, then it is returned to the
data plane server to make an access control check against the identity token.
