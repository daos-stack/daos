# DAOS Control API

The control package exposes an RPC-based API for control plane client
applications to communicate with DAOS Server processes.
The underlying transport mechanism is gRPC and the externally exposed API
request and response structures are converted into protobuf messages internally
(protobuf definitions are not directly exposed to the consumer of the API).

The Invoker interface is exposed in [`rpc.go`](/src/control/lib/control/rpc.go)
which can be implemented by clients to provide capability to invoke unary or
stream RPCs.
Unary RPCs (one request one response) can be issued to a hostlist or, in the
case of a management service (MS) request, just to the MS leader.

The internal logic that will determine where to actually send the request is as
follows.
For an MS request, MS client in the control API first tries a random small
subset of the entire hostlist, if one of those hosts can service the request
then it is handled.
If not, those hosts will respond with a structured error (system.ErrNotReplica
or system.ErrNotLeader) that contains hints as to where the client should try
next.
Non-replica hosts respond with ErrNotReplica and replicas which are not the
leader respond with ErrNotLeader.
In the worst-case scenario a MS request could require 2 RPCs to service.
For server-to-server MS requests, the process is more efficient because each
server has the list of access points in its config file, it can skip the first
RPC and instead send to the APs, one of which should be able to service the
request.
