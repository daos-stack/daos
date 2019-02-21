# DAOS Common Libraries

Common functionality and infrastructure shared across all DAOS components are provided in external shared libraries. This includes the following features:
- Hash and checksum routines
- Event and event queue support for non-blocking operations
- Logging and debugging infrastructure
- Locking primitives
- Network transport

## Task Scheduler Engine (TSE)

## dRPC C API

For a general overview of dRPC concepts and the corresponding Golang API, see [here](/src/control/drpc/README.md).

In the C API, an active dRPC connection is represented by a pointer to a context object (`struct drpc`). The context supplies all the state information required to communicate over the Unix Domain Socket. When finished with a context, the object should be freed by using `drpc_close()`.

dRPC calls and responses are represented by the Protobuf-generated structures `Drpc__Call` and `Drpc__Response`.

### C Client

Connecting to a valid Unix Domain Socket returns a dRPC context, which can be used to execute any number of dRPC calls to the server that set up the socket.

**Note:** Currently synchronous calls (using flag `R_SYNC`) are the only type supported. Asynchronous calls receive an instantaneous response but are never truly processed.

#### Basic Client Workflow

1. Open a connection to the server's Unix Domain Socket:
    ```
    struct drpc *ctx = drpc_connect("/var/run/my_socket.sock");
    ```
2. Send a dRPC call:
    ```
    Drpc__Call *call;
    /* Alloc and set up your Drpc__Call */
    Drpc__Response *resp = NULL; /* Response will be allocated by drpc_call */
    int result = drpc_call(ctx, R_SYNC, call, &resp);
    ```
    An error code returned from `drpc_call()` indicates that the message could not be sent, or there was no response. If `drpc_call()` returned success, the content of the response still needs to be checked for errors returned from the server.
3. Send as many dRPC calls as desired.
4. When finished with the connection, close it: `drpc_close(ctx);`
    **Note**: After `drpc_close()` is called, the dRPC context pointer has been freed and is no longer valid.

### C Server

The dRPC server sets up a Unix Domain Socket and begins listening on it for client connections. In general, this means creating a listening dRPC context to detect any incoming connections. Then, when a client connects, a new dRPC context is generated for that specific session. The session context is the one that actually sends and receives data. It is possible to have multiple session contexts open at the same time.

The socket is always set up as non-blocking, so it is necessary to poll for activity on the context's file descriptor (`ctx->comm->fd`) using a system call like `poll()` or `select()` on POSIX-compliant systems. This applies not only to the listening dRPC context, but also to any dRPC context generated for a specific client session.

The server flow is dependent on a custom handler function, whose job is to dispatch incoming `Drpc__Call` messages appropriately. The handler function should inspect the module and method IDs, ensure that the desired method is executed, and create a `Drpc__Response` based on the results.

#### Basic Server Workflow

1. Set up the Unix Domain Socket at a given path and create a listening context using a custom handler function:
    ```
    void my_handler(Drpc__Call *call, Drpc__Response **resp) {
        /* Handle the message based on module/method IDs */
    }
    ...
    struct drpc *listener_ctx = drpc_listen("/var/run/drpc_socket.sock", my_handler);
    ```
2. Poll on the listener context's file descriptor (`listener_ctx->comm->fd`).
3. On incoming activity, accept the connection:
    ```
    struct drpc *session_ctx = drpc_accept(listener_ctx);
    ```
    This creates a session context for the specific client. All of that client's communications will come over the session context.
4. Poll on the session context's file descriptor (`session_ctx->comm->fd`) for incoming data.
5. On incoming data, handle the message:
    ```
    int result = drpc_recv(session_ctx);
    ```
    This unmarshals the incoming `Drpc__Call`, calls the handler function defined when the listener context was created, and sends the `Drpc__Response` back over the channel to the client. An error will only be returned if the incoming data is not a `Drpc__Call` or the handler failed to allocate a response (for example, if the system is out of memory).
6. If the client has closed the connection, close the session context to free the pointer:
    ```
    drpc_close(session_ctx);
    ```
7. When it's time to shut down the server, close any open session contexts, as noted above. Then `drpc_close()` the listener context.
