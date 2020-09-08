# DAOS Data Plane (aka daos_io_server)

## Module Interface

The I/O server supports a module interface that allows to load server-side code on demand. Each module is effectively a library dynamically loaded by the I/O server via dlopen.
The interface between the module and the I/O server is defined in the `dss_module` data structure.

Each module should specify:
- a module name
- a module identifier from `daos_module_id`
- a feature bitmask
- a module initialization and finalize function

In addition, a module can optionally configure:
- a setup and cleanup function invoked once the overall stack is up and running
- CART RPC handlers
- dRPC handlers

## Thread Model & Argobot Integration

The I/O server is a multi-threaded process using Argobots for non-blocking processing.

By default, one main xstream and no offload xstreams are created per target. The actual number of offload xstream can be configured through daos_io_server command line parameters. Moreover, an extra xstream is created to handle incoming metadata requests. Each xstream is bound to a specific CPU core. The main xstream is the one receiving incoming target requests from both client and the other servers. A specific ULT is started to make progress on network and NVMe I/O operations.

## Thread-local Storage (TLS)

Each xstream allocates private storage that can be accessed via the `dss_tls_get()` function. When registering, each module can specify a module key with a size of data structure that will be allocated by each xstream in the TLS. The `dss_module_key_get()` function will return this data structure for a specific registered module key.

## Incast Variable Integration

DAOS uses IV (incast variable) to share values and statuses among servers under a single IV namespace, which is organized as a tree. The tree root is called IV leader, and servers can either be leaves or non-leaves. Each server maintains its own IV cache. During fetch, if the local cache can not fulfill the request, it forwards the request to its parents, until reaching the root (IV leader). As for update, it updates its local cache first, then forwards to its parents until it reaches the root, which then propagate the changes to all the other servers. The IV namespace is per pool, which is created during pool connection, and destroyed during pool disconnection. To use IV, each user needs to register itself under the IV namespace to get an identification, then it will use this ID to fetch or update its own IV value under the IV namespace.

## dRPC Server

The I/O server includes a dRPC server that listens for activity on a given Unix Domain Socket. See the [dRPC documentation](../control/drpc/README.md) for more details on the basics of dRPC, and the low-level APIs in Go and C.

The dRPC server polls periodically for incoming client connections and requests. It can handle multiple simultaneous client connections via the `struct drpc_progress_context` object, which manages the `struct drpc` objects for the listening socket as well as any active client connections.

The server loop runs in its own User-Level Thread (ULT) in xstream 0. The dRPC socket has been set up as non-blocking and polling uses timeouts of 0, which allows the server to run in a ULT rather than its own xstream. This channel is expected to be relatively low-traffic.

### dRPC Progress

`drpc_progress` represents one iteration of the dRPC server loop. The workflow is as follows:

1. Poll with a timeout on the listening socket and any open client connections simultaneously.
2. If any activity is seen on a client connection:
    1. If data has come in: Call `drpc_recv` to process the incoming data.
    2. If the client has disconnected or the connection has been broken: Free the `struct drpc` object and remove it from the `drpc_progress_context`.
3. If any activity is seen on the listener:
    1. If a new connection has come in: Call `drpc_accept` and add the new `struct drpc` object to the client connection list in the `drpc_progress_context`.
    2. If there was an error: Return `-DER_MISC` to the caller. This causes an error to be logged in the I/O server, but does not interrupt the dRPC server loop. Getting an error on the listener is unexpected.
4. If no activity was seen, return `-DER_TIMEDOUT` to the caller. This is purely for debugging purposes. In practice the I/O server ignores this error code, since lack of activity is not actually an error case.

### dRPC Handler Registration

Individual DAOS modules may implement handling for dRPC messages by registering a handler function for one or more dRPC module IDs.

Registering handlers is simple. In the `dss_server_module` field `sm_drpc_handlers`, statically allocate an array of `struct dss_drpc_handler` with the last item in the array zeroed out to indicate the end of the list. Setting the field to NULL indicates nothing to register. When the I/O server loads the DAOS module, it will register all of the dRPC handlers automatically.

**Note:** The dRPC module ID is **not** the same as the DAOS module ID. This is because a given DAOS module may need to register more than one dRPC module ID, depending on the functionality it covers. The dRPC module IDs must be unique system-wide and are listed in a central header file: `src/include/daos/drpc_modules.h`

The dRPC server uses the function `drpc_hdlr_process_msg` to handle incoming messages. This function checks the incoming message's module ID, searches for a handler, executes the handler if one is found, and returns the `Drpc__Response`. If none is found, it generates its own `Drpc__Response` indicating the module ID was not registered.
