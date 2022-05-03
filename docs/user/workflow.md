# Workflow

## Use Cases

A DAOS pool is a persistent storage reservation allocated to a
project or specific job. Pools are allocated, shrunk, grown and destroyed by
the administrators.

The typical workflow consists of:

- New project members meet and define storage requirements, including space, bandwidth, IOPS & data protection needs.
- Administrators collect those requirements, create a pool for the new project and set relevant ACL to grant access to project members.
- Administrators notify the project members that the pool has been created and provide the pool label to the users.

Users can then create containers (i.e., datasets or buckets) in their pool.
Containers will share the pool space and have their ACL to be managed by
the container owner.

Since pool creation is relatively fast, it is also possible to integrate it
with the resource manager to create and ingest data into an ephemeral pool for
each job.

Another alternative use case is to emulate a parallel file system by creating
one big pool with a single POSIX container to be accessed by all users.

## `daos(1)` Utility

The `daos(1)` utility is built over the `libdaos` library and is the primary
command-line interface for users to interact with their pool and containers.
It supports a `-j` option to generate a parseable json output.

The `daos` utility follows the same syntax as `dmg` (reserved for administrator)
and takes a resource (e.g., pool, container, filesystem) and a command (e.g.
query, create, destroy) plus a set of command-specific options.

```bash
$ daos --help
Usage:
  daos RESOURCE COMMAND [OPTIONS] <command>

daos is a tool that can be used to manage/query pool content,
create/query/manage/destroy a container inside a pool, copy data
between a POSIX container and a POSIX filesystem, clone a DAOS container,
or query/manage an object inside a container.

Application Options:
      --debug    enable debug output
      --verbose  enable verbose output (when applicable)
  -j, --json     enable JSON output

Help Options:
  -h, --help     Show this help message

Available commands:
  container   perform tasks related to DAOS containers (aliases: cont)
  filesystem  POSIX filesystem operations (aliases: fs)
  object      DAOS object operations (aliases: obj)
  pool        perform tasks related to DAOS pools
  version     print daos version
```

## Accessing Your Pool

### Access Validation

To validate the pool can be successfully accessed before running
applications, the daos pool autotest suite can be executed.

To run it against a pool labeled `tank`, run the following command:

```bash
$ daos pool autotest tank
Step Operation               Status Time(sec) Comment
  0  Initializing DAOS          OK      0.000
  1  Connecting to pool         OK      0.070
  2  Creating container         OK      0.000  uuid = ba5c6a78-6ddc-4c7e-a73b-b7574c8d85b8
  3  Opening container          OK      0.060
 10  Generating 1M S1 layouts   OK      2.960
 11  Generating 10K SX layouts  OK      0.130
 20  Inserting 1M 128B values   OK     27.350
 21  Reading 128B values back   OK     26.020
 24  Inserting 1M 4KB values    OK     54.410
 25  Reading 4KB values back    OK     54.380
 28  Inserting 100K 1MB values  OK    605.870
 29  Reading 1MB values back    OK    680.360
 96  Closing container          OK      0.000
 97  Destroying container       OK      0.030
 98  Disconnecting from pool    OK      0.010
 99  Tearing down DAOS          OK      0.000

All steps passed.
```

!!! note
    The command is executed in a development environment,
    performance differences will vary based on your system.

!!! warning
    Smaller pools may show DER_NOSPACE(-1007): 'No space
    on storage target'

### Querying the Pool

Once a pool has been assigned to your project (labeled `tank` in the example
below), you can verify how much space was allocated to your project via the
`daos pool query <pool_label>` command as follows:

```bash
$ daos pool query tank
Pool ada29109-0589-4fb8-9726-1252faea5d01, ntarget=32, disabled=0, leader=0, version=1
Pool space info:
- Target(VOS) count:32
- SCM:
  Total size: 50 GB
  Free: 50 GB, min:1.6 GB, max:1.6 GB, mean:1.6 GB
- NVMe:
  Total size: 0 B
  Free: 0 B, min:0 B, max:0 B, mean:0 B
Rebuild idle, 0 objs, 0 recs
```

In addition to the space information, details on the pool rebuild status and
A number of targets are also provided.

This information can also be retrieved programmatically via the
`daos_pool_query()` function of the libdaos library and python equivalent.

### Pool Attributes

Project-wise information can be stored in pool user attributes (not to be
confused with pool properties). Pool attributes can be manipulated via the
`daos pool [set|get|list|del]-attr` commands.

```bash
$ daos pool set-attr tank project_deadline "September 30, 2025"

$ daos pool list-attr tank
Attributes for pool 004abf7c-26c8-4cba-9059-8b3be39161fc:
Name
----
project_deadline

$ daos pool get-attr tank project_deadline
Attributes for pool 004abf7c-26c8-4cba-9059-8b3be39161fc:
Name             Value
----             -----
project_deadline September 30, 2025

$ daos pool del-attr tank project_deadline

$ daos pool list-attr tank
Attributes for pool 004abf7c-26c8-4cba-9059-8b3be39161fc:
  No attributes found.
```

Pool attributes can be manipulated programmatically via the
`daos_pool_[get|get|list|del]_attr()` functions exported by the libdaos library
and python equivalent (see PyDAOS).
