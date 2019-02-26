# I/O Server

## Module Interface

## Argobot Integration

## Thread-local Storage (TLS)

## Shutdown & Signal Management

## Incast Variable Configuration

## dRPC Server

The I/O server includes a dRPC server that listens for activity on a given Unix Domain Socket. See the [dRPC documentation](src/control/drpc/README.md) for more details on the basics of dRPC, and the low-level APIs in Golang and C.

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
    2. If there was an error: Return `-DER_UNKNOWN` to the caller. This causes an error to be logged in the I/O server, but does not interrupt the dRPC server loop. Getting an error on the listener is unexpected.
4. If no activity was seen, return `-DER_TIMEDOUT` to the caller. This is purely for debugging purposes. In practice the I/O server ignores this error code, since lack of activity is not actually an error case.

### dRPC Handler Registration

Individual DAOS modules may implement handling for dRPC messages by registering a handler function for one or more dRPC module IDs.

Registering handlers is simple. In the `dss_server_module` field `sm_drpc_handlers`, statically allocate an array of `struct dss_drpc_handler` with the last item in the array zeroed out to indicate the end of the list. Setting the field to NULL indicates nothing to register. When the I/O server loads the DAOS module, it will register all of the dRPC handlers automatically.

**Note:** The dRPC module ID is **not** the same as the DAOS module ID. This is because a given DAOS module may need to register more than one dRPC module ID, depending on the functionality it covers. The dRPC module IDs must be unique system-wide and are listed in a central header file: `src/include/daos/drpc_modules.h`

The dRPC server uses the function `drpc_hdlr_process_msg` to handle incoming messages. This function checks the incoming message's module ID, searches for a handler, executes the handler if one is found, and returns the `Drpc__Response`. If none is found, it generates its own `Drpc__Response` indicating the module ID was not registered.
