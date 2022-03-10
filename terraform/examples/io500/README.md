# IO500 Example

This example leverages another example [full_cluster_setup](../full_cluster_setup)
to provision a DAOS cluster and configure the clients so that an IO500 benchmark
may be run.

The default configuration is a very small DAOS cluster consisting of 1 DAOS
server and 1 DAOS client.

Different configurations can be used to deploy larger DAOS clusters.
See [Configuration](#configuration) below.

## Dependencies

If this is the first time you are running this example in your GCP project, there
are some things you will need to do.

Refer to the [images/README.md](../../../images/README.md) for instructions.

After you have followed those instructions you will be ready to run this example.

## Running the example

The example is intended to be run from a Linux system that has access to GCP.

### Deploying the DAOS Cluster

Run [start.sh](start.sh) script:

```bash
cd terraform/examples/io500
./start.sh
```

This will use the default configuration [terraform/examples/io500/config/config.sh](config/config.sh) to run Terraform and deploy the instances.

When the `start.sh` script finishes it will provide further instructions for
logging into the first daos-client instance and running the IO500 benchmark.

### Shutting Down the DAOS Cluster

To destroy the DAOS instances run the [stop.sh](stop.sh) script.

```bash
cd terraform/examples/io500
./stop.sh
```

## Configuration

By default the `start.sh` script sources the
[./config/config.sh](config/config.sh) configuration file.

The [config/config.sh](config/config.sh) file contains environment
variables that control how Terraform will deploy the DAOS cluster.

You can run `start.sh` with `-c <config_file>` to use different configurations
in the [/terraform/examples/io500/config](/terraform/examples/io500/config) directory.

It is recommended that you use the configuration files in the [terraform/examples/io500/configs](/terraform/examples/io500/config) since those have been tested to work.

Changing the values in the config files or using your own configuration may result
in errors that prevent the example from working.

## Scripts

### Configuration

- [terraform/examples/io500/configs](configs/)

  All files in this directory are sourced by other scripts.

  The `config*.sh` files contain environment variables that drive the behavior
  of Terraform and other scripts.

### Executable scripts

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

All other scripts are not intended to be directly run by a user.

These are called by other scripts and used for
- building custom images with the IO500
- configuring instances
- cleaning DAOS storage before each IO500 run
