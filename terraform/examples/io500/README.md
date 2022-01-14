# Setup

## Deployment

1. From your PC:
    1. Configure variables to your needs in [configure.sh](configure.sh) script
    2. Run [start.sh](start.sh) script to deploy DAOS on GCP from your PC
    3. SSH to first DAOS client
2. From first DAOS client:
    1. Run [run_io500-sc21.sh](run_io500-sc21.sh) to finish DAOS environment configuration and benchmark it with IO500
        - you can run this script multiple times to do several IO500 benchmarks
3. From your PC:
    1. Run [stop.sh](stop.sh) to destroy DAOS environment on GCP

## Scripts definition

### Configuration

- [configure.sh](configure.sh)

  This file contains environment variables and is sourced by the other scripts listed below.

  You can make changes to this file in order to customize the machine types,
  number of disks, IO500 stonewall time, etc.

### Scripts that are run by a user

- [start.sh](start.sh)

  Used to deploy DAOS instances on GCP

- [stop.sh](stop.sh)

  Destroy DAOS instances on GCP.

  To avoid unnecessary GCP Compute costs always be sure to run this when
  you are finished using the DAOS environment that was created with `start.sh`

- [run_io500-sc21.sh](run_io500-sc21.sh)

  Should always be run only on the first DAOS client instance.

  - Installs IO500 dependencies (if missing)
  - Configures DAOS environment
  - Runs the IO500 SC21 benchmark
  - Copies results into a timestamped directory

  After you have run start.sh and set up your DAOS server and client instances
  this script can be run multiple times.

### Supporting Scripts

These are not intended to be directly run by a user. They are called by other
scripts.

- [install_mpifileutils.sh](install_mpifileutils.sh)

  Installs a patched version of mpifileutils that allows IO500 to work with
  DAOS.

  Run by the `run_io500-sc21.sh` if mpifileutils is not already installed.
  Run on the DAOS client instances by the `run_io500-sc21.sh` if
  the patched version of mpifileutils is not already installed.

  Required to be run before running `install_io500-sc21.sh`

- [install_io500-sc21.sh](install_io500-sc21.sh)

  Installs IO500 SC21 on the DAOS client nodes.

  Run on the DAOS client instances by the `run_io500-sc21.sh` if
  IO500 SC21 is not already installed.


- [clean.sh](clean.sh)
  lean DAOS environment to run another IO500 benchmark on the same environment and reconfigure DAOS server configuration
