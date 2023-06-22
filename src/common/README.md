# DAOS Common Libraries

Common functionality and infrastructure shared across all DAOS components are provided in external shared libraries. This includes the following features:
- Hash and checksum routines
- Event and event queue support for non-blocking operations
- Logging and debugging infrastructure
- Locking primitives
- Network transport

## Task Scheduler Engine (TSE)

The TSE is a generic library to create generic tasks with function callbacks,
optionally add dependencies between those tasks, and schedule them in an engine
that is progressed to execute those tasks in an order determined by a dependency
graph in which they were inserted in. The task dependency graph is the integral
part of the scheduler to allow users to create several tasks and progress them
in a non-blocking manner.

The TSE is not DAOS specific, but used to be part of the DAOS core and was later
extracted into the common src as a standalone API. The API is generic and allows
creation of tasks in an engine without any DAOS specific functionality. The DAOS
library does provide a task API that is built on top of the TSE. For more
information on that see [here](/src/client/api/README.md). Furthermore, DAOS
uses the TSE internally to track and progress all API tasks that are associated
with the API event and, in some cases, to schedule several in-flight "child"
tasks corresponding to a single API task and add a dependency on that task to
track all those in-flight "child" tasks. An example of that would be the Array
API in the DAOS library and the object update with multiple replicas.

### Scheduler API

The scheduler API allows a user to create a generic scheduler and add tasks to
it. At the time of scheduler creation, the user can register a completion
callback to be called when the scheduler is finalized.

The tasks that are added to the scheduler do not progress on their own. There
has to be explicit calls to a progress function (daos_sched_progress) on the
scheduler to make progress on the tasks in the engine. This progress function
can be called by the user occasionally in their program, or a single thread can
be forked that calls the progress function repeatedly.

### Task API

The task API allows the creation of tasks with generic body functions and adding
them to a scheduler. Once a task is created within a scheduler, it will not be
actually scheduled to run without an explicit call from the user to the task
schedule function, unless it's part of a task dependency graph where in this
case the explicit schedule call is required only to the first task in the
graph. After creating the task, the user can register any number of dependencies
for the task that would be required to complete before the task can be scheduled
to run. In addition, the user will be able to register preparation and
completion callback on the task:

- Preparation Callbacks are executed when the task is ready to run but has not
  been executed yet, meaning the dependencies that the task was created with are
  done and the scheduler is ready to schedule the task. This is useful when the
  task to be scheduled needs information that is not available at the time of
  task creation but will be available after the dependencies of the task
  complete; for example setting some input parameters for the task body
  function.

- Completion Callbacks are executed when the task is finished executing and the
  user needs to do more work or handling when that happens. An example where
  this would be useful is setting the completion of a higher level event or
  request that is built on top of the TSE, or to track error status of multiple
  tasks in a dependency list.

Several other functionality on the task API exists to support:

- setting some private data on the task itself that can be queried.

- pushing and popping data on/from task stack space without data copy

- generic task lists

More detail about that functionality can be found in the TSE header in the DAOS
code [here](/src/include/daos/tse.h).

## dRPC C API

For a general overview of dRPC concepts and the corresponding Go API, see
[here](/src/control/drpc/README.md).

In the C API, an active dRPC connection is represented by a pointer to a context
object (`struct drpc`). The context supplies all the state information
required to communicate over the Unix Domain Socket.

By default a context starts with one reference to it. You can add references to
a given context with `drpc_add_ref()`. When finished with a context, the
object should be released by using `drpc_close()`.  When the last reference is
released, the object is freed.

dRPC calls and responses are represented by the Protobuf-generated structures
`Drpc__Call` and `Drpc__Response`.

### C Client

Connecting to a valid Unix Domain Socket returns a dRPC context, which can be
used to execute any number of dRPC calls to the server that set up the socket.

**Note:** Currently synchronous calls (using flag `R_SYNC`) are the only type
supported. Asynchronous calls receive an instantaneous response but are never
truly processed.

#### Basic Client Workflow

1. Open a connection to the server's Unix Domain Socket:
    ```
    struct drpc *ctx;
    rc = drpc_connect("/var/run/my_socket.sock", &ctx);
    ```
2. Send a dRPC call:
    ```
    Drpc__Call *call;
    /* Alloc and set up your Drpc__Call */
    Drpc__Response *resp = NULL; /* Response will be allocated by drpc_call */
    int result = drpc_call(ctx, R_SYNC, call, &resp);
    ```
    An error code returned from `drpc_call()` indicates that the message could
    not be sent, or there was no response. If `drpc_call()` returned success,
    the content of the response still needs to be checked for errors returned
    from the server.
3. Send as many dRPC calls as desired.
4. When finished with the connection, close it: `drpc_close(ctx);`
    **Note**: After `drpc_close()` is called, the dRPC context pointer has
    been freed and is no longer valid.

### C Server

The dRPC server sets up a Unix Domain Socket and begins listening on it for
client connections. In general, this means creating a listening dRPC context to
detect any incoming connections. Then, when a client connects, a new dRPC
context is generated for that specific session. The session context is the one
that actually sends and receives data. It is possible to have multiple session
contexts open at the same time.

The socket is always set up as non-blocking, so it is necessary to poll for
activity on the context's file descriptor (`ctx->comm->fd`) using a system
call like `poll()` or `select()` on POSIX-compliant systems. This applies not
only to the listening dRPC context, but also to any dRPC context generated for a
specific client session.

The server flow is dependent on a custom handler function, whose job is to
dispatch incoming `Drpc__Call` messages appropriately. The handler function
should inspect the module and method IDs, ensure that the desired method is
executed, and populate a `Drpc__Response` based on the results.

#### Basic Server Workflow

1. Set up the Unix Domain Socket at a given path and create a listening context
using a custom handler function:
    ```
    void my_handler(Drpc__Call *call, Drpc__Response *resp) {
        /* Handle the message based on module/method IDs */
    }
    ...
    struct drpc *listener_ctx = drpc_listen("/var/run/drpc_socket.sock",
                                            my_handler);
    ```
2. Poll on the listener context's file descriptor (`listener_ctx->comm->fd`).
3. On incoming activity, accept the connection:
    ```
    struct drpc *session_ctx = drpc_accept(listener_ctx);
    ```
    This creates a session context for the specific client. All of that client's
    communications will come over the session context.
4. Poll on the session context's file descriptor (`session_ctx->comm->fd`)
for incoming data.
5. On incoming data, receive the message:
    ```
    Drpc__Call *incoming;
    int result = drpc_recv_call(session_ctx, &incoming);
    if (result != 0) {
        /* process errors */
    }
    ```
    This unmarshals the incoming data into a `Drpc__Call`. If the data isn't
    a `Drpc__Call`, it returns an error.
6. Allocate a Drpc__Response and pass it into the session handler.
    ```
    Drpc__Response *resp = drpc_response_create(call);
    session_ctx->handler(call, resp);
    ```
    Your session handler should fill out the response with any errors or
    payloads.
7. Send the response to the caller and clean up:
    ```
    int result = drpc_send_response(session_ctx, resp);
    if (result != 0) {
        /* process errors */
    }
    drpc_response_free(resp);
    drpc_call_free(call);
    ```
8. If the client has closed the connection, close the session context to free
the pointer:
    ```
    drpc_close(session_ctx);
    ```
9. When it's time to shut down the server, close any open session contexts, as
noted above. Then `drpc_close()` the listener context.

## Checksum

### Checksummer

A "Checksummer" is used to create checksums from a scatter gather list. The
checksummer uses a function table (specified when initialized) to adapt common
checksum calls to the underlying library that implements the checksum algorithm.
Currently the isa-l and isa-l_crypto libraries are used to support adler32,
crc16, crc32, crc64, sha1, sha256, and sha512. All of the function tables to
support these algorithms are in
[src/common/multihash_isal.c](multihash_isal.c). These function tables are not
made public, but there is a helper function (daos_mhash_type2algo) that will
return the appropriate function table given a DAOS_CSUM_TYPE. There is another
helper function (daos_contprop2csumtype) that will convert a container property
value to the appropriate DAOS_CSUM_TYPE. The double "lookups" from container
property checksum value to function table was done to remove the coupling from
the checksummer and container info.

All checksummer functions should start with daos_csummer_* and take a struct
daos_csummer as the first argument. To initialize a new daos_csummer,
daos_csummer_init takes the address to a pointer (so memory can be allocated),
the address to a function table implementing the desired checksum algorithm, and
because this is a DAOS checksummer, the size to be used for "chunks" (See
[VOS](/src/vos/README.md) for details on chunks and chunk size). If it wasn't
for the need to break the incoming data into chunks, the checksummer would not
need the chunk size. When done with a checksummer, daos_csummer_destroy should
be called to free allocated resources. Most checksummer functions are simple
passthroughs to the function table if implemented. The main exception is
daos_csummer_calc which, using the other checksummer functions, creates a
checksum from the appropriate memory represented by the scatter gather list
(d_sg_list_t) and the extents (daos_recx_t) of an I/O descriptor (daos_iod_t).
The checksums are put into a collection of checksum buffers (daos_csum_buf_t),
each containing multiple checksums. The memory for the daos_csum_buf_t's and the
checksums will be allocated. Therefore, when done with the checksums,
daos_csummer_destroy_csum_buf should be called to free this memory.

There are a set of helper functions (prefixed with dcb) to act on a
daos_csum_buf_t. These functions should be straight forward. The daos_csum_buf_t
contains a pointer to the first byte of the checksums and information about the
checksums, including count, size of checksum, etc.
