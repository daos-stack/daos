# Protobuf definitions

The proto directory contains [protocol buffer](https://developers.google.com/protocol-buffers) definitions for the messaging format used in communication over the gRPC and dRPC channels. For information on using protobuf tools for development see [here](../../doc/dev/development.md#protobuf-compiler).

When modifying these files, make sure to update all generated files. Relevant files can be generated with commands listed below issued from within the [src/proto](.) top level directory of DAOS source:

* Files generated for the control plane in `src/control` will be in Golang and have the file extension `.pb.go`.
Example command syntax: `protoc -I mgmt --go_out=plugins=grpc:control/common/proto/mgmt mgmt/storage.proto`

* Files generated for the data plane in other `src` subdirectories will be in C and have file extensions `.pb-c.[ch]`.
A third-party plugin [protobuf-c](https://github.com/protobuf-c/protobuf-c) is required to generate C language pb files.
Example command syntax: `protoc -I mgmt --c_out=../mgmt mgmt/srv.proto --plugin=/opt/potobuf/install/bin/protoc-gen-c`

