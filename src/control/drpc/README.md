# dRPC

dRPC is a means of communication between processes local to the same physical system, via a Unix Domain Socket. At any given time a process may act as a client, a server, or both, though each listening dRPC server needs its own Unix Domain Socket.

The server will fail to create the socket if something already exists at that location in the filesystem, even if it is an older incarnation of the socket. Optionally, your application may wish to unlink that filesystem location before creating the socket.

dRPC calls are defined by module and method identifiers. The dRPC module can be thought of as a package of related functions. The dRPC method indicates a specific function to be executed by the server. If the method requires an input, it should be marshalled in the body of the dRPC call. The server will respond with a dRPC response structure, which may include a method-specific response in the body.

The DAOS dRPC implementation is dependent on Protocol Buffers to define the structures passed over the dRPC channel. Any structure to be sent via dRPC as part of a call or response must be [defined in a .proto file](/src/proto).

## Go API

In Go, the drpc package includes both client and server functionality, which is outlined below. For documentation of the C API, see [here](/src/common/README.md).

The dRPC call and response are represented by the Protobuf-generated `drpc.Call` and `drpc.Response` structures.

### Go Client

The dRPC client is represented by the `drpc.ClientConnection` object.

#### Basic Client Workflow

1. Create a new client connection with the path to the dRPC server's Unix Domain Socket:
    ```
    conn := drpc.NewClientConnection("/var/run/my_socket.sock")
    ```
2. Connect to the dRPC server:
    ```
    err := conn.Connect()
    ```
3. Create your `drpc.Call` and send it to the server:
    ```
    call := drpc.Call{}
    // Set up the Call with module, method, and body
    resp, err := drpc.SendMsg(call)
    ```
    An error indicates that the `drpc.Call` couldn't be sent, or an invalid `drpc.Response` was received. If there is no error returned, the content of the `drpc.Response` should still be checked for errors reported by the server.
4. Send as many calls as desired.
5. Close the connection when finished:
    ```
    conn.Close()
    ```

### Go Server

The dRPC server is represented by the `drpc.DomainSocketServer` object.

Individual dRPC modules must be registered with the server in order to handle incoming dRPC calls for that module. To create a dRPC module, create an object that implements the `drpc.Module` interface. The module ID must be unique.

#### Basic Server Workflow

1. Create the new DomainSocketServer with the server's Unix Domain Socket (file permissions 0600):
    ```
    drpcServer, err := drpc.NewDomainSocketServer(log, "/var/run/my_socket.sock", 0600)
    ```
2. Register the dRPC modules that the server needs to handle:
    ```
    drpcServer.RegisterRPCModule(&MyExampleModule{})
    drpcServer.RegisterRPCModule(&AnotherExampleModule{})
    ```
3. Start the server to kick off the Goroutine to start listening for and handling incoming connections:
   ```
   err = drpc.Start()
   ```
4. When it is time to shut down the server, close down the listening Goroutine:
   ```
   drpcServer.Shutdown()
   ```
