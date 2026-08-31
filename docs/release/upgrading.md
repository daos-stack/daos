# Upgrading to DAOS Version 3.0

## Upgrading DAOS from Version 2.8.x to Version 3.0

Upgrading DAOS from Version 2.8.x to DAOS Version 3.0.0
is supported as an offline update,
maintaining the data in DAOS pools and containers.

Environments running DAOS levels older than 2.8.x should be first
upgraded to DAOS Version 2.8.x before upgrading to DAOS Version 3.0.

The recommended procedure for the upgrade is the following:

### Review Interoperability Requirements

This upgrading procedure assumes that all DAOS servers, clients and
admin nodes are updated at the same time.
If not all nodes are updated at the same time, refer to
[DAOS Version Interoperability](version_interop.md)
to determine which DAOS levels on the DAOS servers, clients and admin nodes
are interoperable and plan the sequence of upgrades accordingly.

### Stop the DAOS system

Follow the steps in [Stopping a DAOS system](../admin/stopping-daos.md)
to completely stop the running DAOS system.

### Perform all necessary software updates

- On all servers, admin nodes, and clients,
  perform any required OS updates and/or high-speed fabric stack updates.
  Refer to the [DAOS 3.0 Support Matrix](support_matrix.md) for details
  on the supported levels.

- On all servers, admin nodes, and clients,
  perform the RPM update to the new DAOS 3.0.0 fix level.
  Set up the new DAOS repository, then run
  `dnf update` (EL9), or `zypper update` (SLES/OpenSUSE)
  to update the DAOS RPMs and their dependencies.

- In Slingshot environments, if the SHS version has been updated as part
  of the OS and high-speed fabric update, then the LD\_LIBRARY\_PATH must
  be updated to point to the new version of the SHS-provided libfabric.
  This is needed in the engine environment section of `daos_server.yml`
  on the servers, and in the user environment settings on the clients.
  See [High Speed Fabric Support](../admin/fabrics.md) for details.

### Restart DAOS

Follow the steps in [Starting a DAOS system](../admin/starting-daos.md)
to start DAOS after the upgrade.

### Update DAOS pool versions to newest version

New DAOS software versions may introduce new DAOS pool versions,
and newly created pools will automatically use the newest pool version.
Please refer to [DAOS Version Interoperability](version_interop.md)
to determine if the highest DAOS pool version changed in this release,
and to check what the steps are to update older pools to the newest
pool version (or to migrate data from older pools to a newly created
pool if in-place updating of the pool version is not possible).

## Upgrading DAOS from Version older than 2.8.x to Version 3.0

DAOS provides "N-1" interoperability. To update from DAOS versions older than
Version 2.8.x to Version 3.0, please update the system to the 2.8.x fixlevel
first before updating to DAOS 3.0.

