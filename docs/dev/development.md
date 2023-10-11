# Development Environment

This section covers specific instructions to create a developer-friendly
environment to contribute to the DAOS development. This includes how to
regenerate the protobuf files or add new Go package dependencies, which is
only required for development purposes.

## Building DAOS for Development

The DAOS repository is hosted on [GitHub](https://github.com/daos-stack/daos).
To checkout the current development version, simply run:

```bash
$ git clone --recurse-submodules https://github.com/daos-stack/daos.git
```

For a specific branch or tag (e.g. v1.2), add `-b v1.2` to the command
line above.

Prerequisite when built using `--build-deps` are installed in component
specific directories under PREFIX/prereq/$TARGET_TYPE.

Run the following `scons` command:

```bash
$ scons PREFIX=${daos_prefix_path}
      install
      --build-deps=yes
      --config=force
```

Installing the components into separate directories allow upgrading the
components individually by replacing `--build-deps=yes` with
`--update-prereq={component\_name}`. This requires a change to the environment
configuration from before. For automated environment setup, source
`utils/sl/setup_local.sh`.

The install path should be relocatable with the exception that `daos_server_helper`
will not be able to find the new location of daos and dependencies. All other
libraries and binaries should work without any change due to relative
paths.  Editing the `.build-vars.sh` file to replace the old with the new can
restore the capability of setup_local.sh to automate path setup.

To run daos_server, either the rpath in daos_server_helper needs to be patched to
the new installation location of `spdk` and `isal` or `LD_LIBRARY_PATH` needs to
be set.  This can be done using `SL_SPDK_PREFIX` and `SL_ISAL_PREFIX` set when
sourcing `setup_local.sh`.   This can also be done with the following
commands:

```
source utils/sl/setup_local.sh
sudo -E utils/setup_daos_server_helper.sh [path to new location of daos]
```

This script is intended only for developer setup of `daos_server_helper`.

With this approach, DAOS gets built using the prebuilt dependencies in
`${daos_prefix_path}/prereq`, and required options are saved for future compilations.
So, after the first time, during development, only "`scons --config=force`" and
"`scons --config=force install`" would suffice for compiling changes to DAOS
source code.

If you wish to compile DAOS with clang rather than `gcc`, set `COMPILER=clang` on
the scons command line.   This option is also saved for future compilations.

Additionally, users can specify `BUILD_TYPE=[dev|release|debug]` and scons will
save the intermediate build for the various `BUILD_TYPE`, `COMPILER`, and `TARGET_TYPE`
options so a user can switch between options without a full rebuild and thus
with minimal cost.   By default, `TARGET_TYPE` is set to `'default'` which means
it uses the `BUILD_TYPE` setting.  To avoid rebuilding prerequisites for every
`BUILD_TYPE` setting, `TARGET_TYPE` can be explicitly set to a `BUILD_TYPE` setting
to always use that set of prerequisites.  These settings are stored in daos.conf
so setting the values on subsequent builds is not necessary.

If needed, `ALT_PREFIX` can be set to a colon separated prefix path where to
look for already built components.  If set, the build will check these
paths for components before proceeding to build.

### Custom build targets

The DAOS build also supports build targets to customize what parts of DAOS are
built.  At present, just three such targets are defined, `client`, `server`, and
`test`.

To build only client libraries and tools, use the following command:

```bash
$ scons [args] client install
```

To build the server instead, substitute `server` for `client` in the above
command.

Note that such targets need to be specified each time you build as the default
is equivalent to specifying `client server test` on the command line.  The
`test` target is, at present, dependent on `client` and `server` as well.

### Stack analyzer

When using gcc compiler, the DAOS build contains a tool to generate a stack usage
report for each function.  It reports the size, in bytes, of the stack frame added by each
function in DAOS.

The report is enabled using the `--analyze-stack="[arg] ..."` option.

To get usage information for this option, run

```bash
$ scons COMPILER=gcc --analyze-stack="-h"
```

The tool normally runs post build but the `-e` option can be added to run it immediately and exit
as in the following example:

```bash
$ scons COMPILER=gcc --analyze-stack="-e -c 1024 -x tests" -Q
```

One should only use this option if a prior build with gcc has been executed.  The `-Q` option to
scons reduces the clutter from compiler setup.

Additionally, the tool supports options to filter by directory and file names and specify a lower
bound value to report.

## Go dependencies

Developers contributing Go code may need to change the external dependencies
located in the `src/control/vendor` directory. The DAOS codebase uses
[Go Modules](https://github.com/golang/go/wiki/Modules) to manage these
dependencies. As this feature is built in to Go distributions starting with
version 1.11, no additional tools are needed to manage dependencies.

Among other benefits, one of the major advantages of using Go Modules is that it
removes the requirement for builds to be done within the `$GOPATH`, which
simplifies our build system and other internal tooling.

While it is possible to use Go Modules without checking a vendor directory into
SCM, the DAOS project continues to use vendored dependencies in order to
insulate our build system from transient network issues and other problems
associated with nonvendored builds.

The following is a short list of example workflows. For more details, please
refer to [one](https://github.com/golang/go/wiki/Modules#quick-start) of
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

In all cases, after updating the vendor directory, it is a good idea to verify
that your changes were applied as expected. In order to do this, a simple
workflow is to clear the caches to force a clean build and then run the test
script, which is vendor-aware and will not try to download missing modules:

```bash
$ cd ~/daos/src/control # or wherever your daos clone lives
$ go clean -modcache -cache
$ ./run_go_tests.sh
$ ls ~/go/pkg/mod # ~/go/pkg/mod should either not exist or be empty
```

## Protobuf Compiler

The DAOS control plane infrastructure uses [Protocol Buffers](https://github.com/protocolbuffers/protobuf)
as the data serialization format for its RPC requests. Not all developers will
need to compile the `\*.proto` files, but if Protobuf changes are needed, the
developer must regenerate the corresponding C and Go source files using a
Protobuf compiler compatible with proto3 syntax.

### Recommended Versions

The recommended installation method is to clone the git repositories, check out
the tagged releases noted below, and install from source. Later versions may
work, but are not guaranteed.  You may encounter installation errors when
building from source relating to insufficient permissions.  If that occurs,
you may try relocating the repo to `/var/tmp/` in order to build and install from there.

- [Protocol Buffers](https://github.com/protocolbuffers/protobuf) v3.11.4. [Installation instructions](https://github.com/protocolbuffers/protobuf/blob/master/src/README.md).
- [Protobuf-C](https://github.com/protobuf-c/protobuf-c) v1.3.3. [Installation instructions](https://github.com/protobuf-c/protobuf-c/blob/master/README.md).
- gRPC plugin: [protoc-gen-go](https://github.com/golang/protobuf) is the version specified in [go.mod](https://github.com/daos-stack/daos/blob/master/src/control/go.mod). This plugin is automatically installed by the Makefile in $DAOSREPO/src/proto.

### Compiling Protobuf Files

The source (`.proto`) files live under `$DAOSREPO/src/proto`. The preferred
mechanism for generating compiled C/Go protobuf definitions is to use the
Makefile in this directory. Care should be taken to keep the Makefile updated
when source files are added or removed, or generated file destinations are
updated.

Note that the generated files are checked into SCM and are not generated as part
of the normal DAOS build process. This allows developers to ensure that the
generated files are correct after any changes to the source files are made.

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

After verifying that the generated C/Go files are correct, add and commit them
as you would any other file.

## DAOS Development in Docker

This section describes how to build and run the DAOS service in a Docker
container. A minimum of 5GB of DRAM and 16GB of disk space will be required.
On Mac, please make sure that the Docker settings under
"Preferences/{Disk, Memory}" are configured accordingly.

### Building a Docker Image

To build the Docker image directly from GitHub, run the following command:

```bash
$ docker build https://github.com/daos-stack/daos.git#master \
        -f utils/docker/Dockerfile.el.8 -t daos
```

or from a local tree:

```bash
$ docker build  . -f utils/docker/Dockerfile.el.8 -t daos
```

This creates a Rocky Linux 8 image, fetches the latest DAOS version from GitHub,
builds it, and installs it in the image.
For Ubuntu and other Linux distributions, replace Dockerfile.el.8 with
Dockerfile.ubuntu or the appropriate version of interest.

### Simple Docker Setup

Once the image created, one can start a container that will eventually run
the DAOS service:

```bash
$ docker run -it -d --privileged --cap-add=ALL --name server -v /dev:/dev daos
```

!!! note
    If you want to be more selective with the devices that are exported to the
    container, individual devices should be listed and exported as volume via
    the -v option. In this case, the hugepages devices should also be added
    to the command line via -v /dev/hugepages:/dev/hugepages and
    -v /dev/hugepages-1G:/dev/hugepages-1G

!!! warning
    If Docker is being run on a non-Linux system (e.g., OSX), -v /dev:/dev
    should be removed from the command line.

The `daos_server_local.yml` configuration file sets up a simple local DAOS
system with a single server instance running in the container. By default, it
uses 4GB of DRAM to emulate persistent memory and 16GB of bulk storage under
/tmp. The storage size can be changed in the yaml file if necessary.

The DAOS service can be started in the docker container as follows:

```bash
$ docker exec server daos_server start \
        -o /home/daos/daos/utils/config/examples/daos_server_local.yml
```

!!! note
    Please make sure that the uio_pci_generic module is loaded on the host.

Once started, the DAOS server waits for the administrator to format the system.
This can be triggered in a different shell, using the following command:

```bash
$ docker exec server dmg -i storage format
```

Upon successful completion of the format, the storage engine is started, and pools
can be created using the daos admin tool (see next section).

For more advanced configurations involving SCM, SSD or a real fabric, please
refer to the next section.
