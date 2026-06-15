# DAOS Control Plane

DAOS operates over two, closely integrated planes, Control and Data.  The Data
plane handles the heavy lifting transport operations while the Control plane
orchestrates process and storage management, facilitating the operation of the
Data plane.

The DAOS Control Plane is written in Go and runs as the DAOS Server
(`daos_server`) process. It is tasked with network and storage hardware
provisioning and allocation in addition to instantiation and management of the
DAOS Data Plane (Engine) processes that run on the same host.

## Code Organization

The control directory contains a "cmd" subdirectory for server, agent, ddb, and
dmg applications. These applications import the control API
(`src/control/lib/control`) or server packages along with peripheral shared
packages common, drpc, fault, logging, and security where necessary to provide
the given features.

Specific library packages can be found in lib/ which provide access to native
storage libraries through language bindings, e.g. lib/spdk or specific
formatting capabilities e.g., lib/hostlist or lib/txtfmt.

The events package provides the golang component of the RAS framework for
receipt of events over dRPC from the DAOS Engine and forwarding of management
service actionable events to the MS leader.

The pbin package provides a framework for forwarding of requests to be executed
by the privileged binary `daos_server_helper` on behalf of `daos_server`.

The provider package contains interface shims to the external environment,
initially just to the Linux operating system.

The system package encapsulates the concept of the DAOS system, and its
associated membership.

## Debugging Go Code

When building DAOS in debug mode, the Go binaries are compiled with optimizations
and inlining disabled (`-gcflags "all=-N -l"`). This makes them fully debuggable
with any Go-aware debugger such as [Delve (dlv)](https://github.com/go-delve/delve)
or GDB with Go support. Without this flag, debuggers warn about optimized
functions, source lines are not correctly mapped to the execution pointer, and
local variables may be shown as `optimized away`.

!!! note
    Release builds are unaffected — the debugger-friendly flags are only
    applied to debug builds. The resulting binaries will be larger and slower,
    which is expected and acceptable in a debug context.

### Installing Delve

```bash
go install github.com/go-delve/delve/cmd/dlv@latest
```

For installation options, platform requirements, and troubleshooting, refer to
the [official Delve documentation](https://github.com/go-delve/delve/tree/master/Documentation).

### Useful dlv commands

Once inside a `dlv` session, the following commands are commonly used:

| Command | Description |
|---|---|
| `break <symbol>` | Set a breakpoint at a function or method |
| `continue` | Run until the next breakpoint |
| `args` | Print all function arguments at the current breakpoint |
| `locals` | Print all local variables at the current breakpoint |
| `print <expr>` | Evaluate and print any expression or variable |
| `step` | Step into the next source line (follows function calls) |
| `next` | Step over the next source line (stays in current function) |
| `goroutines` | List all running goroutines |
| `goroutine <id>` | Switch to a specific goroutine |
| `quit` | Exit the debugger |

Breakpoint symbols follow the Go fully-qualified naming convention:
- Package function: `github.com/daos-stack/daos/src/control/server.FuncName`
- Method: `github.com/daos-stack/daos/src/control/server.(*TypeName).MethodName`
- `main` package (binaries): `main.FuncName` or `main.(*TypeName).MethodName`

### Attaching to a running process

Use `dlv attach` to connect to an already-running `daos_server` or `daos_agent`
process. This is useful for inspecting live behavior without restarting the
service — for example, tracing a pool operation while the system is running.

```bash
dlv attach $(pgrep -x daos_server)
```

!!! warning
    `daos_server` spawns engine subprocesses for the data plane. The Go
    control plane (`mgmtSvc`) runs exclusively in the parent `daos_server` process.
    `pgrep -x daos_server` returns that parent PID; engine child processes are
    separate C programs and cannot be debugged with `dlv`.

Once attached, set a breakpoint and trigger the code path from another terminal:

```
(dlv) break github.com/daos-stack/daos/src/control/server.(*mgmtSvc).resolvePoolID
(dlv) continue
# In another terminal: dmg pool list
(dlv) args        # shows the pool ID being resolved
(dlv) locals      # shows local variables
(dlv) step        # steps line by line through the function
```

!!! warning
    Attaching to a process may require `ptrace` privileges. If `dlv`
    reports a permission error, run it with `sudo` or set
    `/proc/sys/kernel/yama/ptrace_scope` to `0`.

### Launching a binary under dlv

Use `dlv exec` to start a binary under the debugger from scratch. This is useful
when you want to inspect initialization code or set breakpoints before any code
runs. The binary must be a debug build.

```bash
dlv exec $(which dmg) -- pool list
```

!!! warning
    `$(which dmg)` must resolve to the debug build. If a system-installed
    `dmg` is in your `$PATH`, provide the path explicitly instead
    (e.g. `install/bin/dmg`).

Set a breakpoint before letting the program run. For a `pool list` invocation,
`main.(*poolListCmd).Execute` is the most relevant entry point:

```
(dlv) break main.(*poolListCmd).Execute
(dlv) continue
(dlv) args     # shows the parsed pool list options
(dlv) locals   # shows local variables inside the Execute function
(dlv) next
```

The same approach works for `daos_agent`:

```bash
dlv exec $(which daos_agent) -- -o /etc/daos/daos_agent.yml
```

```
(dlv) break main.(*startCmd).Execute
(dlv) continue
(dlv) print cmd   # inspect the full configuration struct
```

### Debugging unit tests with run_go_tests.sh

`src/control/run_go_tests.sh` is the standard script for running Go linters and
tests for the control plane. It handles the full CGo environment setup
(headers and libraries from the DAOS build) automatically by sourcing
`.build_vars.sh`, so no manual configuration of `CGO_CFLAGS`, `CGO_LDFLAGS`, or
`LD_LIBRARY_PATH` is needed.

#### Supported options

| Option | Description |
|---|---|
| `--dlv <package>` | Launch an interactive `dlv` session for a single package instead of running the full test suite. The package must be a fully-qualified Go package path. |
| `--run <TestName>` | Filter to a specific test function (only valid with `--dlv`). Passed as `-test.run` to the binary. Accepts Go regex patterns. |
| `-h`, `--help` | Print usage and exit. |

Any other arguments are forwarded directly to the test runner (`go test` or
`gotestsum` if available).

#### Running all tests

Running the script without arguments lints and tests the entire control plane:

```bash
src/control/run_go_tests.sh
```

The following build tags are applied automatically to include all testable code:

| Tag | What it enables |
|---|---|
| `firmware` | Firmware management command and handler |
| `fault_injection` | Fault injection support in server and client library |
| `test_stubs` | C library stubs (`libdaos`, `libgurt`) that avoid linking against the real shared libraries during unit tests |
| `spdk` | SPDK NVMe and BDev bindings |

If `gotestsum` is installed and available in `$PATH`, it is used automatically
for structured JUnit XML output. Otherwise the script falls back to `go test`.

#### Debugging a specific test

The `--dlv` flag drops you into an interactive `dlv` session for the given
package, with the build tags and CGo environment already configured. `dlv` must
be installed and available in `$PATH`.

```bash
# From the repo root
src/control/run_go_tests.sh --dlv \
    --run TestFaultComparison \
    github.com/daos-stack/daos/src/control/fault
```

Once inside the session, set a breakpoint on the function of interest:

```
(dlv) break github.com/daos-stack/daos/src/control/fault.(*Fault).Equals
(dlv) continue
(dlv) args     # shows receiver 'f' and argument 'raw' with their concrete values
(dlv) step     # steps to the next source line
```

The `--run` flag accepts Go regex patterns, so you can target a group of related
tests at once:

```bash
src/control/run_go_tests.sh --dlv \
    --run TestServer_MgmtSvc_Pool \
    github.com/daos-stack/daos/src/control/server
```

#### Debugging all tests in a package

Omitting `--run` runs every test in the package under `dlv`. This is useful when
you are not sure which test triggers the code path you want to inspect — set a
breakpoint upfront and let the test suite hit it naturally:

```bash
src/control/run_go_tests.sh --dlv \
    github.com/daos-stack/daos/src/control/fault
```

```
(dlv) break github.com/daos-stack/daos/src/control/fault.(*Fault).Equals
(dlv) continue    # runs until any test hits the breakpoint
(dlv) args
```

## Developer Documentation

Please refer to package-specific README's.

- [server](/src/control/server/README.md)
- [godoc reference](https://godoc.org/github.com/daos-stack/daos/src/control)

## User Documentation

- [online documentation](https://docs.daos.io/latest/)
