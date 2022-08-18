# Upgrading to DAOS Version 2.2


## Upgrading DAOS from Version 2.2.x to Version 2.2.y

Upgrading DAOS from one 2.2.x fix level to a newer 2.2.y fix level is
supported as an offline update, maintaining the data in DAOS pools and
containers.

The recommended procedure for the upgrade is:

- Ensure that there are no client applications with open pool connections.
  If necessary, the `dmg pool evict` command can be used on an admin node
  to disconnect any active pool connections.

- Stop the `daos_agent` daemons on all client nodes,
  for example by running `systemctl stop daos_agent` in a parallel shell.

- Stop the DAOS engines by running `dmg system stop` on one admin node.

- Stop the `daos_server` daemons on all server nodes,
  for example by running `systemctl stop daos_server` in a parallel shell.

- On all servers, admin nodes, and clients,
  perform the RPM update to the new DAOS fix level.

- Start the `daos_server` daemons on all server nodes,
  for example by running `systemctl start daos_server` in a parallel shell.

- Validate that all engines have started successfully,
  for example using `dmg system query -v` on an admin node.

- Start the `daos_agent` daemons on all client nodes,
  for example by running `systemctl start daos_agent` in a parallel shell.

DAOS fix levels include all previous fix levels. So it is possible to update
from Version 2.2.0 to Version 2.2.2 without updating to Version 2.2.1 first.


## Upgrading DAOS from Version 2.0 to Version 2.2

### DAOS servers running CentOS 7.9, RHEL 7.9, RHEL 8.4 EUS, SLES 15.3, or openSUSE Leap 15.3

Upgrading from DAOS Version 2.0.x to DAOS Version 2.2.y is supported as an offline update,
maintaining the data in DAOS pools and containers.

The recommended procedure for the upgrade is:

- Ensure that there are no client applications with open pool connections.
  If necessary, the `dmg pool evict` command can be used on an admin node
  to disconnect any active pool connections.

- Stop the `daos_agent` daemons on all client nodes,
  for example by running `systemctl stop daos_agent` in a parallel shell.

- Stop the DAOS engines by running `dmg system stop` on one admin node.

- Stop the `daos_server` daemons on all server nodes,
  for example by running `systemctl stop daos_server` in a parallel shell.

- On all servers, admin nodes, and clients,
  perform the RPM update to the new DAOS fix level.

- **On all admin nodes, install the new `daos-admin` RPM package.**
  (In DAOS 2.0 the `dmg` command was shipped in the `daos-client` RPM.
  In DAOS 2.2 the `dmg` command has been moved into its own RPM package,
  to more clearly separate the DAOS node roles.)

- Start the `daos_server` daemons on all server nodes,
  for example by running `systemctl start daos_server` in a parallel shell.

- Validate that all engines have started successfully,
  for example using `dmg system query -v` on an admin node.

- Start the `daos_agent` daemons on all client nodes,
  for example by running `systemctl start daos_agent` in a parallel shell.

DAOS fix levels include all previous fix levels. So it is possible to update
from Version 2.0.x to Version 2.2.y without updating to Version 2.2.0 first.

### DAOS Servers running CentOS Linux 8, RHEL 8.3, or RHEL 8.5

Due to the end of life of CentOS Linux 8, DAOS servers running DAOS Version 2.0
on CentOS Linux 8 need to be reinstalled with a supported EL8 operating system
(Rocky Linux 8.6, RHEL 8.4 EUS, or RHEL 8.6) in order to use DAOS Version 2.2.
The RHEL 8.3 and RHEL 8.5 releases are also no longer supported.

The process of reinstalling a DAOS server's EL8 operating system while maintaining
the data on PMem and NVMe has not been validated, and is not supported with DAOS 2.2.
This implies that the upgrade to DAOS Version 2.2 in those environments is essentially
a new installation, without maintaining the data in DAOS pools and containers.
Please refer to the
[Upgrading to DAOS Version 2.0](https://docs.daos.io/v2.0/release/upgrading_to_v2_0/)
document for further information on how to save existing user data before the update.


## Upgrading DAOS from Version 1.x to Version 2.2

Note that there is **no** backwards compatibility of DAOS Version 2.x with
DAOS Version 1.y, so an upgrade from DAOS Version 1.0 or 1.2 to
DAOS Version 2.2 is essentially a new installation. Please refer to the
[Upgrading to DAOS Version 2.0](https://docs.daos.io/v2.0/release/upgrading_to_v2_0/)
document for further information - those notes apply to DAOS Version 2.2 as well.
