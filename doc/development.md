# DAOS for Development

# Building DAOS for Development
For development, it is recommended to build and install each dependency in a unique subdirectory. The DAOS build system supports this through the TARGET\_PREFIX variable. Once the submodules have been initialized and updated, run the following:

```
    scons PREFIX=${daos_prefix_path} TARGET_PREFIX=${daos_prefix_path}/opt install --build-deps=yes --config=force
```

Installing the components into seperate directories allow to upgrade the components individually replacing --build-deps=yes with --update-prereq={component\_name}. This requires change to the environment configuration from before. For automated environment setup, source scons_local/utils/setup_local.sh.

```
    ARGOBOTS=${daos_prefix_path}/opt/argobots
    CART=${daos_prefix_path}/opt/cart
    HWLOC=${daos_prefix_path}/opt/hwloc
    MERCURY=${daos_prefix_path}/opt/mercury
    PMDK=${daos_prefix_path}/opt/pmdk
    OMPI=${daos_prefix_path}/opt/ompi
    OPA=${daos_prefix_path}/opt/openpa
    PMIX=${daos_prefix_path}/opt/pmix
    FIO=${daos_prefix_path}/opt/fio
    SPDK=${daos_prefix_path}/opt/spdk

    PATH=$CART/bin/:$OMPI/bin/:${daos_prefix_path}/bin/:$PATH
```

With this approach DAOS would get built using the prebuilt dependencies in ${daos_prefix_path}/opt and required options are saved for future compilations. So, after the first time, during development, a mere "scons --config=force" and "scons --config=force install" would suffice for compiling changes to daos source code.

If you wish to compile DAOS with clang rather than gcc, set COMPILER=clang on the scons command line.   This option is also saved for future compilations.

## Go dependencies

Developers contributing Go code may need to change the external dependencies located in the src/control/vendor directory. The DAOS codebase uses [dep](https://github.com/golang/dep) to manage these dependencies.

On EL7 and later:

```
    yum install yum-plugin-copr
    yum copr enable hnakamur/golang-dep
    yum install golang-dep
```

On Fedora 27 and later:

```
    dnf install dep
```

On Ubuntu 18.04 and later:

```
    apt-get install go-dep
```

For OSes that don't supply a package:
* Ensure that you have a personal GOPATH (see "go env GOPATH", referred to as "$GOPATH" in this document) and a GOBIN ($GOPATH/bin) set up and included in your PATH:

```
    mkdir -p $GOPATH/bin
    export PATH=$GOPATH/bin:$PATH
```

* Then follow the [installation instructions on Github](https://github.com/golang/dep).

To update the vendor directory using dep after changing Gopkg.toml, first make sure DAOS is cloned into

```
    $GOPATH/src/github.com/daos-stack/daos
```

Then:

```
    cd $GOPATH/src/github.com/daos-stack/daos/src/control
    dep ensure
```

## Protobuf Compiler

The DAOS control plane infrastructure uses [Protocol Buffers](https://github.com/protocolbuffers/protobuf) as the data serialization format for its RPC requests. Not all developers will need to compile the *.proto files, but if Protobuf changes are needed, the developer must regenerate the corresponding C and Go source files using a Protobuf compiler compatible with proto3 syntax.

### Recommended Versions

The recommended installation method is to clone the git repositories, check out the tagged releases noted below, and install from source. Later versions may work, but are not guaranteed.

- [Protocol Buffers](https://github.com/protocolbuffers/protobuf) v3.5.1. [Installation instructions](https://github.com/protocolbuffers/protobuf/blob/master/src/README.md).
- [Protobuf-C](https://github.com/protobuf-c/protobuf-c) v1.3.1. [Installation instructions](https://github.com/protobuf-c/protobuf-c/blob/master/README.md).
- gRPC plugin: [protoc-gen-go](https://github.com/golang/protobuf) v1.2.0. **Must match the proto version in src/control/Gopkg.toml.** Install the specific version using GIT_TAG instructions [here](https://github.com/golang/protobuf/blob/master/README.md).

### Compiling Protobuf Files

Generate the Go file using the gRPC plugin. You can designate the directory location:

```
	protoc myfile.proto --go_out=plugins=grpc:<go_file_dir>
```

Generate the C files using Protobuf-C. As the header and source files in DAOS are typically kept in separate locations, you will need to move them manually to their destination directories:

```
	protoc-c myfile.proto --c_out=.
	mv myfile.pb-c.h <c_file_include_dir>
	mv myfile.pb-c.c <c_file_src_dir>
```
