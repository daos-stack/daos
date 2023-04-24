# raft-grpc-transport

[![Godoc](https://godoc.org/github.com/Jille/raft-grpc-transport?status.svg)](https://godoc.org/github.com/Jille/raft-grpc-transport)

This library provides a [Transport](https://godoc.org/github.com/hashicorp/raft#Transport) for https://github.com/hashicorp/raft over gRPC.

One benefit of this is that gRPC is easy to multiplex over a single port.

## Usage

```go
// ...
tm := transport.New(raft.ServerAddress(myAddress), []grpc.DialOption{grpc.WithInsecure()})
s := grpc.NewServer()
tm.Register(s)
r, err := raft.NewRaft(..., tm.Transport())
// ...
```

Want more example code? Check out main.go at https://github.com/Jille/raft-grpc-example
