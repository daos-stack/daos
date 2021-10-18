# Setup

## Deployment

1. From your PC:
    1. Configure variables to your needs in [configure.sh](configure.sh) script
    2. Run [start.sh](start.sh) script to deploy DAOS on GCP from your PC
    3. SSH to first DAOS client
2. From first DAOS client:
    1. Run [setup_io500.sh](setup_io500.sh) to finish DAOS environment configuration and benchmark it with IO500
        - you can run this script multiple times to do several IO500 benchmarks
3. From your PC:
    1. Run [stop.sh](stop.sh) to destroy DAOS environment on GCP

## Scripts definition

[configure.sh](configure.sh) script has all the DAOS configuration that you need to adjust to your needs. It is sourced it other scripts.

[start.sh](start.sh) script is used to deploy DAOS instances on GCP

[setup_io500.sh](setup_io500.sh) script is used to finish DAOS environment configuration and benchmark it with IO500

[stop.sh](stop.sh) script is used to destroy DAOS instances on GCP

[clean.sh](clean.sh) script is used clean DAOS environment to run another IO500 benchmark on the same environment and reconfigure DAOS server configuration
