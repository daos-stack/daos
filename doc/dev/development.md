# Development Environment

This section covers specific instructions to create a developer-friendly
environment to contribute to the DAOS development. This includes how to
regenerate the protobuf files or add new Go package dependencies, which is
only required for development purposes.

## Building DAOS for Development

For development, it is recommended to build and install each dependency in a
unique subdirectory. The DAOS build system supports this through the
TARGET\_PREFIX variable. Once the submodules have been initialized and updated,
run the following commands:

```bash
$ scons PREFIX=${daos_prefix_path}
      TARGET_PREFIX=${daos_prefix_path}/opt install
      --build-deps=yes
      --config=force
```

Installing the components into seperate directories allow upgrading the
components individually by replacing --build-deps=yes with
--update-prereq={component\_name}. This requires a change to the environment
configuration from before. For automated environment setup, source
scons_local/utils/setup_local.sh.

```bash
ARGOBOTS=${daos_prefix_path}/opt/argobots
CART=${daos_prefix_path}/opt/cart
FIO=${daos_prefix_path}/opt/fio
FUSE=${daos_prefix_path}/opt/fuse
ISAL=${daos_prefix_path}/opt/isal
MERCURY=${daos_prefix_path}/opt/mercury
OFI=${daos_prefix_path}/opt/ofi
OPENPA=${daos_prefix_path}/opt/openpa
PMDK=${daos_prefix_path}/opt/pmdk
PROTOBUFC=${daos_prefix_path}/opt/protobufc
SPDK=${daos_prefix_path}/opt/spdk


LD_LIBRARY_PATH=${daos_prefix_path}/opt/spdk/lib:${daos_prefix_path}/opt/protobufc/lib:${daos_prefix_path}/opt/pmdk/lib:${daos_prefix_path}/opt/openpa/lib:${daos_prefix_path}/opt/ofi/lib:${daos_prefix_path}/opt/mercury/lib:${daos_prefix_path}/opt/isal/lib:${daos_prefix_path}/opt/fuse/lib64:${daos_prefix_path}/opt/cart/lib:${daos_prefix_path}/opt/argobots/lib
PATH=${daos_prefix_path}/opt/spdk/bin:${daos_prefix_path}/opt/pmdk/bin:${daos_prefix_path}/opt/ofi/bin:${daos_prefix_path}/opt/isal/bin:${daos_prefix_path}/opt/fio/bin:${daos_prefix_path}/opt/cart/bin
```

With this approach, DAOS would get built using the prebuilt dependencies in
${daos_prefix_path}/opt, and required options are saved for future compilations.
So, after the first time, during development, only "scons --config=force" and
"scons --config=force install" would suffice for compiling changes to DAOS
source code.

## Go dependencies

Developers contributing Go code may need to change the external dependencies
located in the src/control/vendor directory. The DAOS codebase uses
[dep](https://github.com/golang/dep) to manage these dependencies.

On EL7 and later:

```bash
$ yum install yum-plugin-copr
$ yum copr enable hnakamur/golang-dep
$ yum install golang-dep
```

On Fedora 27 and later:

```bash
$ dnf install dep
```

On Ubuntu 18.04 and later:

```bash
$ apt-get install go-dep
```

For OSes that don't supply a package:

-    Ensure that you have a personal GOPATH (see "go env GOPATH", referred to as
"$GOPATH" in this document) and a GOBIN ($GOPATH/bin) set up and included in
your PATH:

```bash
$ mkdir -p $GOPATH/bin
$ export PATH=$GOPATH/bin:$PATH
```

- Then follow the [installation instructions on Github](https://github.com/golang/dep).

To update the vendor directory using dep after changing Gopkg.toml, make
sure DAOS is cloned into "$GOPATH/src/github.com/daos-stack/daos"

Then:

```bash
$ cd $GOPATH/src/github.com/daos-stack/daos/src/control
$ dep ensure
```

## Protobuf Compiler

The DAOS control plane infrastructure uses protobuf as the data serialization
format for its RPC requests. The DAOS proto files use protobuf 3 syntax, which is
not supported by the platform protobuf compiler in all cases. Not all developers
will need to build the proto files into the various source files.
However, if changes are made to the proto files, the corresponding C and Go
source files will need to be regenerated with a protobuf 3.\* or higher
compiler.

The recommended installation method is to clone the git repositories, check out
the tagged releases noted below, and install from source. Later versions may
work but are not guaranteed.

- [Protocol Buffers](https://github.com/protocolbuffers/protobuf) v3.5.1.
  [Installation instructions](https://github.com/protocolbuffers/protobuf/blob/master/src/README.md).
- [Protobuf-C](https://github.com/protobuf-c/protobuf-c) v1.3.1.
  [Installation instructions](https://github.com/protobuf-c/protobuf-c/blob/master/README.md).
- gRPC plugin: [protoc-gen-go](https://github.com/golang/protobuf) v1.2.0.
  Must match the proto version in src/control/Gopkg.toml.
  Install the specific version using GIT_TAG instructions [here](https://github.com/golang/protobuf/blob/master/README.md).

Generate the Go file using the gRPC plugin. You can designate the directory
location:

```bash
$ protoc myfile.proto --go_out=plugins=grpc:<go_file_dir>
```

Generate the C files using Protobuf-C. As the header and source files in DAOS
are typically kept in separate locations, you will need to move them manually
to their destination directories:

```bash
$ protoc-c myfile.proto --c_out=.
$ mv myfile.pb-c.h <c_file_include_dir>
$ mv myfile.pb-c.c <c_file_src_dir>
```
