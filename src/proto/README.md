# Protobuf definitions

The proto directory contains [protocol buffer](https://developers.google.com/protocol-buffers) definitions for the messaging format used in communication over the gRPC and dRPC channels.

When modifying these files, make sure to update any generated files. Relevant files can be generated with commands listed below issued from within the 'proto' top level directory of DAOS source:

* In 'src/control' (generated files will be in Golang language and have extension `.pb.go`).
e.g. `protoc -I mgmt --go_out=plugins=grpc:control/common/proto/mgmt mgmt/storage.proto`

* In other 'src' subdirectories the generated files will be in C language and have extension `.pb-c.[ch]`.
A third-party plugin [protobuf-c](https://github.com/protobuf-c/protobuf-c) is required to generate C language pb files.
e.g. `~/protobuf/install/bin/protoc -I mgmt --c_out=/home/tanabarr/projects/daos/src/mgmt mgmt/srv.proto --plugin=/home/tanabarr/protobuf/install/bin/protoc-gen-c`

