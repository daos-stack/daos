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
    OPA=${daos_prefix_path}/opt/openpa
    FIO=${daos_prefix_path}/opt/fio
    SPDK=${daos_prefix_path}/opt/spdk

    PATH=$CART/bin/:${daos_prefix_path}/bin/:$PATH
```

With this approach DAOS would get built using the prebuilt dependencies in ${daos_prefix_path}/opt and required options are saved for future compilations. So, after the first time, during development, a mere "scons --config=force" and "scons --config=force install" would suffice for compiling changes to daos source code.

If you wish to compile DAOS with clang rather than gcc, set COMPILER=clang on the scons command line.   This option is also saved for future compilations.

## Go dependencies

Developers contributing Go code may need to change the external dependencies located in the src/control/vendor
directory. The DAOS codebase uses [Go Modules](https://github.com/golang/go/wiki/Modules) to manage these
dependencies. As this feature is built in to Go distributions starting with version 1.11, no
additional tools are needed to manage dependencies.

Among other benefits, one of the major advantages of using Go Modules is that it removes the requirement
for builds to be done within the $GOPATH, which simplifies our build system and other internal tooling.

While it is possible to use Go Modules without checking a vendor directory into SCM, the DAOS project
continues to use vendored dependencies in order to insulate our build system from transient network
issues and other problems associated with nonvendored builds.

The following is a short list of example workflows. For more details, please refer to
[one](https://github.com/golang/go/wiki/Modules#quick-start) of
[the](https://engineering.kablamo.com.au/posts/2018/just-tell-me-how-to-use-go-modules/)
[many](https://blog.golang.org/migrating-to-go-modules) resources available online.

```bash
# add a new dependency
$ cd ~/daos/src/control # or wherever your daos clone lives
$ go get github.com/awesome/thing
# make sure that github.com/awesome/thing is imported somewhere in the codebase
$ ./run_go_tests.sh
# note that go.mod and go.sum have been updated automatically
#
# when ready to commit and push for review:
$ go mod vendor
$ git commit -a # should pick up go.mod, go.sum, vendor/*, etc.
```

```bash
# update an existing dependency
$ cd ~/daos/src/control # or wherever your daos clone lives
$ go get -u github.com/awesome/thing
# make sure that github.com/awesome/thing is imported somewhere in the codebase
$ ./run_go_tests.sh
# note that go.mod and go.sum have been updated automatically
#
# when ready to commit and push for review:
$ go mod vendor
$ git commit -a # should pick up go.mod, go.sum, vendor/*, etc.
```

```bash
# replace/remove an existing dependency
$ cd ~/daos/src/control # or wherever your daos clone lives
$ go get github.com/other/thing
# make sure that github.com/other/thing is imported somewhere in the codebase,
# and that github.com/awesome/thing is no longer imported
$ ./run_go_tests.sh
# note that go.mod and go.sum have been updated automatically
#
# when ready to commit and push for review:
$ go mod tidy
$ go mod vendor
$ git commit -a # should pick up go.mod, go.sum, vendor/*, etc.
```

In all cases, after updating the vendor directory, it is a good idea to verify that your
changes were applied as expected. In order to do this, a simple workflow is to clear the
caches to force a clean build and then run the test script, which is vendor-aware and
will not try to download missing modules:

```bash
$ cd ~/daos/src/control # or wherever your daos clone lives
$ go clean -modcache -cache
$ ./run_go_tests.sh
$ ls ~/go/pkg/mod # ~/go/pkg/mod should either not exist or be empty
```

## Protobuf Compiler

The DAOS control plane infrastructure uses [Protocol Buffers](https://github.com/protocolbuffers/protobuf) as the data serialization format for its RPC requests. Not all developers will need to compile the *.proto files, but if Protobuf changes are needed, the developer must regenerate the corresponding C and Go source files using a Protobuf compiler compatible with proto3 syntax.

### Recommended Versions

The recommended installation method is to clone the git repositories, check out the tagged releases noted below, and install from source. Later versions may work, but are not guaranteed.

- [Protocol Buffers](https://github.com/protocolbuffers/protobuf) v3.5.1. [Installation instructions](https://github.com/protocolbuffers/protobuf/blob/master/src/README.md).
- [Protobuf-C](https://github.com/protobuf-c/protobuf-c) v1.3.1. [Installation instructions](https://github.com/protobuf-c/protobuf-c/blob/master/README.md).
- gRPC plugin: [protoc-gen-go](https://github.com/golang/protobuf) v1.3.4. **Must match the proto version in
src/control/go.mod.** Install the specific version using GIT_TAG instructions
[here](https://github.com/golang/protobuf/blob/master/README.md).

### Compiling Protobuf Files

The source (.proto) files live under $DAOSREPO/src/proto. The preferred mechanism for generating
compiled C/Go protobuf definitions is to use the Makefile in this directory. Care should be taken
to keep the Makefile updated when source files are added or removed, or generated file destinations
are updated.

Note that the generated files are checked into SCM and are not generated as part of the normal DAOS
build process. This allows developers to ensure that the generated files are correct after any changes
to the source files are made.

```bash
$ cd ~/daos/src/proto # or wherever your daos clone lives
$ make
protoc -I /home/foo/daos/src/proto/mgmt/ --go_out=plugins=grpc:/home/foo/daos/src/control/common/proto/mgmt/ acl.proto
protoc -I /home/foo/daos/src/proto/mgmt/ --go_out=plugins=grpc:/home/foo/daos/src/control/common/proto/mgmt/ mgmt.proto
...
$ git status
...
#       modified:   ../control/common/proto/mgmt/acl.pb.go
#       modified:   ../control/common/proto/mgmt/mgmt.pb.go
...
```

After verifying that the generated C/Go files are correct, add and commit them as you would any other
file.