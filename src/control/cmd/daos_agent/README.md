# DAOS Agent

The DAOS Agent is a background process that serves as a trusted intermediary
between client applications and the DAOS system. A DAOS Agent process must be
running on every compute node in order for client operations on those nodes to
execute successfully.

## Certificate

If the system is set up in normal mode (not insecure mode), the agent will have
been configured with a certificate with the CommonName "agent" to identify
itself and attest the validity of user credentials.

For more details on how certificates are used within DAOS, see the
[Security documentation](/src/control/security/README.md#certificate-usage-in-daos).

## Client Communications

Client processes communicate with the DAOS Agent using a UNIX Domain Socket
set up on the compute node.

### UNIX Domain Socket

The default directory used for the UNIX Domain Socket is
`/var/run/daos_agent`. Alternately, a directory may be specified in the agent
configuration file. The directory must exist, and the user starting the agent
process must have write access to it in order for the agent to start up
successfully. The agent opens the socket and proceeds to listen for
communications from local clients.

The DAOS client library does not read in configuration files. If using a
non-default location for the socket, this directory must be specified in the
environment variable `DAOS_AGENT_DRPC_DIR` in order for the client library
to communicate with the agent.

### dRPC

The protocol used to communicate between the client and the agent is
[dRPC](/src/control/drpc/README.md). The required dRPC communications are baked
into the client library and should be invisible to client API users.

The agent acts as a dRPC server exclusively, while the client API acts as a
dRPC client. The agent does not query the client library. The agent's dRPC
handlers are implemented in the `*_rpc.go` files in this directory.

## Server Communications

Communications between the agent and the DAOS Control Plane Server occur over
the management network, via the gRPC protocol. The access point servers are
defined in the agent's config file.

For details on how gRPC communications are secured and authenticated, see the
[Security documentation](/src/control/security/README.md#host-authentication-with-certificates).

## Functionality

The functions provided by the agent include:

- Getting attachment info for the DAOS server ranks.
- Generating a signed client credential to be used by the server for access
  control decisions.

These functions are accessed by the client via the
[dRPC protocol](#client-communications).

### Get Attach Info

Client communications are sent over the high-speed fabric to data plane engine.
Initially the client has no knowledge of the URIs of these server ranks.
The Primary Service Ranks (PSRs) are engines that the client may query
to get the URI for a particular rank in the cluster. Once the PSRs are known to
the client library and an appropriate network device has been selected, client
communications over CaRT are initialized and will automatically query a PSR to
direct RPCs to the correct rank.

To get the PSRs and the network configuration, the client process must send a
Get Attach Info request to the agent. The first time the agent process receives
the request, it initializes a cache of Get Attach Info responses.  To populate
the cache, the agent forwards this request to an access point's Control Plane
Server via the management network. The Control Plane Server forwards the
request again to a local data plane instance, which looks up the PSRs and
returns the information back to the agent. The agent then performs a local
fabric scan to determine which network devices are available that support the
fabric provider in use by daos_server. It determines the NUMA affinity for each
matching network device found. The agent stores a fully encoded response into
the cache that encapsulates the PSRs and the network configuration per NUMA
node.  Multiple network devices per NUMA node are supported. Each device and
NUMA node combination has its own entry in the cache.

At this point, the Get Attach Info cache is initialized.  For this request and
all subsequent requests, the agent will examine the client PID associated with
the request to determine its NUMA binding, if available.  The agent then
indexes into the cache to retrieve a cached response with a network device that
matches the client's NUMA affinity and returns that back to the client, without
communicating further up the stack.  If the client NUMA affinity cannot be
determined, or if no available network devices share that same affinity, a
default response is chosen from the known network devices.  If there are no
network devices, a response encoded with the loopback device is chosen instead.

If there are multiple network devices available that share the same NUMA
affinity, the cache will contain an entry for each.  The agent uses a
round-robin selection algorithm to choose the responses within the same NUMA
node.

The Get Attach Info payload contains the network configuration parameters which
include the D_INTERFACE, D_DOMAIN, CRT_TIMEOUT, provider, and
CRT_CTX_SHARE_ADDR.  The D_INTERFACE, D_DOMAIN and CRT_TIMEOUT may be
overridden by setting any of these environment variables in the client
environment prior to launch.  The daos client library will initialize CaRT with
the values provided by the Get Attach Info request unless overridden.

The client requires this information in order to send any RPCs.

### Request Client Credentials

Certain client operations (such as connecting to a pool) are gated by access
controls. Because the client library could be tampered with or replaced, it
cannot be trusted to perform its own access checks or to generate its own
credentials. Instead, the client must query the agent to verify the identity of
the effective user. This identity information is used by the server to make
access control checks.

To request a credential for their user identity, the client sends a Request
Credentials request to the agent. The agent inspects the UNIX Domain Socket
used to send the request to get the UID of the effective user. From
this, it produces an Auth Token. An Auth Token is a credential payload and a
verifier value that can be used to confirm the integrity of the credential. The
Auth Token is returned to the client library, which packs it into a binary blob.
This can be sent to a Data Plane Server as part of an RPC payload.

In secure mode, the verifier is a signature on the packed credential,
using the private key for the agent's certificate. When the Data Plane Server
receives this credential, it can verify the signature using the set of known
agent certificates that each server possesses. This guarantees that the
credential was generated by the agent and not tampered with.

In insecure mode, the verifier is merely a hash of the credential data. This
can verify that the credential was not corrupted in transit, but otherwise
provides no protection from tampering.
