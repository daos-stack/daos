# Upgrading to DAOS Version 2.8

## Upgrading DAOS from Version 2.6.5 to Version 2.8

Upgrading DAOS from Version 2.6.5 to DAOS Version 2.8.0
is supported as an offline update,
maintaining the data in DAOS pools and containers.

Environments running DAOS levels older than 2.6.5 should be first
upgraded to DAOS Version 2.6.5 before upgrading to DAOS Version 2.8.

The recommended procedure for the upgrade is the following:

### Review Interoperability Requirements

This upgrading procedure assumes that all DAOS servers, clients and
admin nodes are updated at the same time.
If not all nodes are updated at the same time, refer to
[DAOS Version Interoperability](version_interop.md)
to determine which DAOS levels on the DAOS servers, clients and admin nodes
are interoperable and plan the sequence of upgrades accordingly.

### Stop the DAOS system

Follow the steps in [Stopping a DAOS system)](../admin/stopping-daos.md)
to completely stop the running DAOS system.

### Perform all necessary software updates

- On all servers, admin nodes, and clients,
  perform any required OS updates and/or high-speed fabric stack updates.
  Refer to the [DAOS 2.8 Support Matrix](support_matrix.md) for details
  on the supported levels.

- On all servers, admin nodes, and clients,
  perform the RPM update to the new DAOS 2.8.0 fix level.
  Set up the new DAOS repository, then run
  `dnf update` (EL9), or `zypper update` (SLES/OpenSUSE)
  to update the DAOS RPMs and their dependencies.

!!! note
    On the DAOS servers, the `--allowerasing` option is needed due to
    a re-packaging of the SPDK toolset: In previous releases SPDK was
    provided in `spdk` RPMs. DAOS 2.8 provides SPDK in `daos-spdk` RPMs,
    which can only be installed if the older `spdk` RPMs are erased.

!!! note
    The new SPDK version 26.01 in DAOS 2.8 changes some of the
    parameter names in the `daos_nvme.conf` configuration file.
    In case of a _downgrade_ from DAOS 2.8 to the older DAOS release,
    the `daos_nvme.conf` file needs to be manually changed back.
    See the "Known Issues" section of the
    [DAOS 2.8 Release Notes](release_notes.md) for details.

- On systems using libfabric, install the new `mercury-libfabric` RPM
  on all servers and clients. In mercury-2.4.1 the libfabric provider is
  not included in the base mercury-2.4.1 RPM but can be optionally installed
  as a separate `mercury-libfabric` package (similar to the `mercury-ucx`
  package for UCX). Failing to install this package will cause errors at
  startup when the `ofi` provider is used, similar to this one:

```
  mercury->cls [fatal] /home/daos/pre/build/external/release/mercury/src/na/na.c:645 na_plugin_open() Could not open lib /usr/lib64/mercury/libna_plugin_ofi.so, libfabric.so.1: cannot open shared object file: No such file or directory
  mercury->cls [fatal] /home/daos/pre/build/external/release/mercury/src/na/na.c:570 na_plugin_scan_path() Could not open plugin (libna_plugin_ofi.so)
```

- In Slingshot environments, if the SHS version has been updated as part
  of the OS and high-speed fabric update, then the LD\_LIBRARY\_PATH must
  be updated to point to the new version of the SHS-provided libfabric.
  This is needed in the engine environment section of `daos_server.yml`
  on the servers, and in the user environment settings on the clients.
  See [High Speed Fabric Support](../admin/fabrics.md) for details.

- On all server nodes, make sure that transparent hugepages are disabled
  in grub for the Linux kernel command line, and reboot if necessary.
  Run `cat /proc/cmdline|grep transparent_hugepage=never`
  to check if the setting is in effect.
  If transparent hugepages are enabled, the DAOS server will not start:

```
  N0001 ERROR 2026/01/22 14:10:31 server: code = 623 resolution = "disable THP by adding 'transparent_hugepage=never' kernel parameter in the grub configuration file then reboot and restart daos_server"
```

### Restart DAOS

Follow the steps in [Starting a DAOS system](../admin/starting-daos.md)
to start DAOS after the upgrade.

### Updating DAOS pool versions to newest version

New DAOS software versions may introduce new DAOS pool versions,
and newly created pools will automatically use the newest pool version.
Please refer to [DAOS Version Interoperability](version_interop.md)
to determine if the highest DAOS pool version changed in this release,
and to check what the steps are to update older pools to the newest
pool version (or to migrate data from older pools to a newly created
pool if in-place updating of the pool version is not possible).

## Upgrading DAOS from Version older than 2.6.5 to Version 2.8

DAOS provides "N-1" interoperability. To update from DAOS versions older than
Version 2.6.5 to Version 2.8, please update the system to the 2.6.5 fixlevel
first before updating to DAOS 2.8.

