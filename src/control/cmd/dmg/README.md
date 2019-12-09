# Management Tool (TO BE UPDATED)

Go app which can either be invoked with a subcommand to perform a specific task or without a subcommand which will launch an interactive shell.
Command-line subcommands are implemented with the [go-flags](https://github.com/jessevdk/go-flags) package and interactive shell with [ishell](https://github.com/abiosoft/ishell).

The management tool uses the [client API](../../client) to interact with many [server](../../server) instances as a gRPC client.
The management tool has no storage library dependencies and as such is suitable to be run from a login node to interact with storage nodes.

<details>
<summary>Usage info from app help</summary>
<p>

```bash
[tanabarr@ssh-1 ~]$ projects/daos_m/install/bin/dmg --help
Usage:
  dmg [OPTIONS] [command]

Application Options:
  -l, --hostlist=    comma separated list of addresses <ipv4addr/hostname:port> (default: localhost:10001)
  -f, --hostfile=    path of hostfile specifying list of addresses <ipv4addr/hostname:port>, if specified takes preference over HostList
  -o, --config-path= Client config file path

Help Options:
  -h, --help         Show this help message

Available commands:
  network  Perform tasks related to locally-attached network devices (aliases: n)
  pool     Perform tasks related to DAOS pools (aliases: p)
  service  Perform distributed tasks related to DAOS system (aliases: sv)
  storage  Perform tasks related to locally-attached storage (aliases: st)

[tanabarr@ssh-1 ~]$ projects/daos_m/install/bin/dmg storage --help
Usage:
  dmg [OPTIONS] storage <list>

Application Options:
  -l, --hostlist=    comma separated list of addresses <ipv4addr/hostname:port> (default: localhost:10001)
  -f, --hostfile=    path of hostfile specifying list of addresses <ipv4addr/hostname:port>, if specified takes preference over HostList
  -o, --config-path= Client config file path

Help Options:
  -h, --help         Show this help message

Available commands:
  list  List locally-attached SCM and NVMe storage (aliases: l)

[tanabarr@ssh-1 ~]$ projects/daos_m/install/bin/dmg service --help
Usage:
  dmg [OPTIONS] service <kill-rank>

Application Options:
  -l, --hostlist=    comma separated list of addresses <ipv4addr/hostname:port> (default: localhost:10001)
  -f, --hostfile=    path of hostfile specifying list of addresses <ipv4addr/hostname:port>, if specified takes preference over HostList
  -o, --config-path= Client config file path

Help Options:
  -h, --help         Show this help message

Available commands:
  kill-rank  Terminate server running as specific rank on a DAOS pool (aliases: kr)

[tanabarr@ssh-1 ~]$ projects/daos_m/install/bin/dmg service kill-rank --help
Usage:
  dmg [OPTIONS] service kill-rank [kill-rank-OPTIONS]

Application Options:
  -l, --hostlist=      comma separated list of addresses <ipv4addr/hostname:port> (default: localhost:10001)
  -f, --hostfile=      path of hostfile specifying list of addresses <ipv4addr/hostname:port>, if specified takes preference over HostList
  -o, --config-path=   Client config file path

Help Options:
  -h, --help           Show this help message

[kill-rank command options]
      -r, --rank=      Rank identifying DAOS server
      -p, --pool-uuid= Pool uuid that rank relates to
```

</p>
</details>

## Subcommands

### storage scan

<details>
<summary>Example output from invoking "storage scan" subcommand on a single host</summary>
<p>

```bash
[root@wolf-72 ~]# /root/daos_m/install/bin/dmg storage scan
Active connections: [localhost:10001]

Listing NVMe SSD controller and constituent namespaces on connected storage servers:
localhost:10001:
- id: 0
  model: 'INTEL SSDPED1K375GA '
  serial: 'PHKS7335008X375AGN  '
  pciaddr: 0000:87:00.0
  fwrev: E2010324
  namespace:
  - id: 1
    capacity: 375
- id: 0
  model: 'INTEL SSDPEDMD016T4 '
  serial: 'CVFT6010002F1P6DGN  '
  pciaddr: 0000:81:00.0
  fwrev: 8DV10171
  namespace:
  - id: 1
    capacity: 1600
- id: 0
  model: 'INTEL SSDPEDMD016T4 '
  serial: 'CVFT5392000G1P6DGN  '
  pciaddr: 0000:da:00.0
  fwrev: 8DV10171
  namespace:
  - id: 1
    capacity: 1600


Listing SCM modules on connected storage servers:
localhost:10001:
- physicalid: 28
  channel: 0
  channelpos: 1
  memctrlr: 0
  socket: 0
  capacity: 539661172736
- physicalid: 40
  channel: 0
  channelpos: 1
  memctrlr: 1
  socket: 0
  capacity: 539661172736
- physicalid: 50
  channel: 0
  channelpos: 1
  memctrlr: 0
  socket: 1
  capacity: 539661172736
- physicalid: 62
  channel: 0
  channelpos: 1
  memctrlr: 1
  socket: 1
  capacity: 539661172736
```

</p>
</details>

<details>
<summary>Example output from invoking "storage scan" subcommand on multiple hosts</summary>
<p>

```bash
[tanabarr@ssh-1 ~]$ projects/daos_m/install/bin/dmg -l boro-44:10001,boro-45:10001 storage scan
Active connections: [boro-45:10001 boro-44:10001]


Listing NVMe SSD controller and constituent namespaces on connected storage servers:
boro-44:10001:
- id: 0
  model: 'INTEL SSDPED1K375GA '
  serial: 'PHKS73350016375AGN  '
  pciaddr: 0000:81:00.0
  fwrev: E2010324
  namespace:
  - id: 1
    capacity: 375
boro-45:10001:
- id: 0
  model: 'INTEL SSDPED1K375GA '
  serial: 'PHKS7335006W375AGN  '
  pciaddr: 0000:81:00.0
  fwrev: E2010420
  namespace:
  - id: 1
    capacity: 375


Listing SCM modules on connected storage servers:
boro-44:10001: []
boro-45:10001: []
```

</p>
</details>

### storage format

<details>
<summary>Example output from invoking "storage format" subcommand</summary>
<p>

```bash
[tanabarr@ssh-1 ~]$ dmg storage format
2019/06/19 15:51:44 config.go:122: debug: DAOS Client config read from /home/tanabarr/projects/daos_m/install/etc/daos.yml
Active connections: [boro-45:10001]

This is a destructive operation and storage devices specified in the server config file will be erased.
Please be patient as it may take several minutes.


Listing NVMe storage format results on connected storage servers:
boro-45:10001:
- pciaddr: ""
  state:
    status: 0
    error: ""
    info: no controllers specified


Listing SCM storage format results on connected storage servers:
boro-45:10001:
- mntpoint: /mnt/daos
  state:
    status: 0
    error: ""
    info: status=CTL_SUCCESS
```

</p>
</details>

## Interactive shell

<details>
<summary>Example output when listing storage in interactive mode</summary>
<p>

```bash
[tanabarr@ssh-1 ~]$ projects/daos_m/install/bin/dmg
Active connections: [localhost:10001]

DAOS Management Shell
>>> help

Commands:
  addconns          Command to create connections to servers by supplying a space separated list of addresses <ipv4addr/hostname:port>
  clear             clear the screen
  clearconns        Command to clear stored server connections
  exit              exit the program
  getconns          Command to list active server connections
  help              display help
  killrank          Command to terminate server running as specific rank on a DAOS pool
  listfeatures      Command to retrieve supported management features on connected servers
  liststorage       Command to list locally-attached NVMe SSD controllers and SCM modules


>>> addconns boro-44:10001 boro-45:10001
failed to connect to localhost:10001 (socket connection is not active (TRANSIENT_FAILURE))
Active connections: [boro-45:10001 boro-44:10001]

>>> liststorage
Active connections: [boro-45:10001 boro-44:10001]

Listing NVMe SSD controller and constituent namespaces on connected storage servers:
boro-44:10001:
- id: 0
  model: 'INTEL SSDPED1K375GA '
  serial: 'PHKS73350016375AGN  '
  pciaddr: 0000:81:00.0
  fwrev: E2010324
  namespace:
  - id: 1
    capacity: 375
boro-45:10001:
- id: 0
  model: 'INTEL SSDPED1K375GA '
  serial: 'PHKS7335006W375AGN  '
  pciaddr: 0000:81:00.0
  fwrev: E2010420
  namespace:
  - id: 1
    capacity: 375


Listing SCM modules on connected storage servers:
boro-44:10001: []
boro-45:10001: []
```

</p>
</details>
