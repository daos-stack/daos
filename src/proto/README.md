# Protobuf definitions

The proto directory contains
[protocol buffer](https://developers.google.com/protocol-buffers) definitions
for the messaging format used in communication over the gRPC and dRPC channels.
For information on using protobuf tools for development see
[here](../../docs/dev/development.md#protobuf-compiler).

## Directory structure

* `ctl` directory is for messages that can be multicast to DAOS Servers that
have gRPC server capacity, regardless of the state of the DAOS system.
* `mgmt` directory is for messages that need to be handled by the DAOS
management service leader, this requires that the DAOS system be initialized.
* `security` directory is for messages specific to the context of security.
* `shared` directory contains message definitions shared between
subdirectories.
* `srv` directory contains message definitions specific to communications from
DAOS I/O Engine processes to DAOS Server processes via
[dRPC](/src/control/drpc/README.md).

## Generate language specific code

When modifying these files, make sure to update all generated files.
For convenience, existing proto generated code will be updated by running
`make` from the `src/proto` directory.
Be sure to edit the [`Makefile`](/src/proto/Makefile) when adding or moving
`.proto` files.

Go language specific protobuf message definitions are generated into the
[common proto](/src/control/common/proto) directory with the same directory
structure.

Alternatively, auto generated protobuf files can be updated or created with commands listed below issued from within the [src/proto](.) top level directory of DAOS source:

* Files generated for the control plane in `src/control` will be in Golang and
have the file extension `.pb.go`.
Example command syntax:
`protoc -I mgmt --go_out=plugins=grpc:control/common/proto/mgmt mgmt/storage.proto`

* Files generated for the data plane in other `src` subdirectories will be in C
and have file extensions `.pb-c.[ch]`.
A third-party plugin [protobuf-c](https://github.com/protobuf-c/protobuf-c) is
required to generate C language pb files.
Example command syntax:
`protoc -I mgmt --c_out=../mgmt mgmt/srv.proto --plugin=/opt/potobuf/install/bin/protoc-gen-c`
