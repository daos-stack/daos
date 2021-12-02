# Upgrading to DAOS Version 2.0

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

When upgrading to DAOS Version 2.0, please also verify the supported
operating system levels as outlined in the
[DAOS Version 2.0 Support](./support_matrix_v2_0.md) document.
If an OS upgrade is required, this should be performed prior to upgrading DAOS.
