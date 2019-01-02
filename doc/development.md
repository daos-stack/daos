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

## Golang dependencies

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

The DAOS control plane infrastructure uses protobuf as the data serialization format for its RPC requests. The DAOS proto files use protobuf 3 syntax which is not supported by the platform protobuf compiler in all cases. Not all developers will need to build the proto files into the various source files. However if changes are made to the proto files they will need to be regenerated with a protobuf 3.* or higher compiler. To setup support for compiling protobuf files download the following precompiled package for Linux and install it somewhere accessible by your PATH variable.

    https://github.com/google/protobuf/releases/download/v3.5.1/protoc-3.5.1-linux-x86_64.zip
