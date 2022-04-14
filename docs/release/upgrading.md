# Upgrading to DAOS Version 2.0


## Upgrading DAOS from Version 2.0.x to Version 2.0.y

Upgrading DAOS from one 2.0.x fix level to a newer 2.0.y fix level is
supported as an offline update, maintaining the data in DAOS pools and
containers.

The recommended procedure for the upgrade is:

- Ensure that there are no client applications with open pool connections.
  If necessary, the `dmg pool evict` command can be used to disconnect
  any active pool connections.
- Stop the `daos_agent` daemons.
- Stop the DAOS engines by running `dmg system stop`.
- Stop the `daos_server` daemons.
- Perform the RPM update to the new DAOS fix level.
- Start the `daos_server` daemons.
- Validate that all engines have started successfully,
  for example using `dmg system query -v`.
- Start the `daos_agent` daemons.

DAOS fix levels include all previous fix levels. So it is possible to updating
from Version 2.0.0 to Version 2.0.2 without updating to Version 2.0.1 first.


## Upgrading DAOS from Version 1.x to Version 2.0

DAOS Version 2.0 is a major feature release, and it was a conscious
design decision to **not** provide backwards compatibility with previous
DAOS releases in favor of new feature development and stabilization.
For future DAOS releases, a limited form of backward compatibility
between two adjacent releases is planned.

DAOS Version 2.0 introduces several protocol and API changes,
as well as changes in the internal object layout,
which make DAOS Version 2.0 incompatible with previous DAOS releases.
It is therefore not possible to perform online or offline upgrades from
previous DAOS releases to DAOS Version 2.0, while maintaining the user
data in containers that have been created with previous DAOS releases.

This means that an upgrade from DAOS Version 1.0 or 1.2 to
DAOS Version 2.0 is essentially a new installation.
Existing user data in DAOS should be backed up to non-DAOS storage
before the upgrade, as the DAOS storage will need to be reformatted.
For POSIX containers, there are two paths to perform such a backup:

1. On a DAOS client where the DAOS POSIX container is dfuse-mounted,
the _serial_ Linux `cp` command can be used to copy out the data.
To improve throughput, it is adviable to use the DAOS I/O interception library.
Running `LD_PRELOAD=/usr/lib64/libioil.so cp $SOURCE $DEST` will provide
significantly better performance than just running `cp $SOURCE $DEST`.

2. For larger data volumes, the _MPI-parallel_ tools from
[mpiFileUtils](https://hpc.github.io/mpifileutils/) can be used.
Note that the DAOS backend for mpiFileUtils was only added in
DAOS Version 2.0. So for the purpose of backing up data from
DAOS Version 1.0 or 1.2 containers, it is best to build mpiFileUtils
**without** DAOS support. The `dcp` parallel copy command can then be run
in its normal POSIX mode on the DAOS POSIX container, which needs to be
dfuse-mounted on all nodes on which the MPI-parallel `dcp` job is run.
Similar to the serial `cp` case, it is advisable to use `dcp` in conjunction
with the DAOS I/O interception library to achieve higher throughput.
To do this, the `LD_PRELOAD` setting must be passed to all tasks
of the MPI-parallel job, for example with
`mpirun -genv LD_PRELOAD /usr/lib64/libioil.so -np 8 -f hostfile dcp $SOURCE $DEST`.

When planning the upgrade to DAOS Version 2.0, please also verify the supported
operating system levels as outlined in the
[DAOS Version 2.0 Support](./support_matrix.md) document.
If an OS upgrade is required, this should be performed prior to upgrading DAOS.

!!! note
    Note that CentOS 8.3 uses `hwloc-1.11`, while CentOS 8.4 uses `hwloc-2.2`.
    DAOS requires the hwloc-1 functionality, and DAOS Version 2.0 includes the
    `compat-hwloc1` RPM that addresses this version change. Older DAOS releases
    do not include this compatibility package. So updating CentOS 8.3 to
    CentOS 8.4 while an older DAOS release is installed will likely fail with
    a dependency error. Adding the DAOS 2.0 packages repository and updating
    DAOS to Version 2.0 at the same time as performing the CentOS update
    should eliminate this problem.
