# IO500 Example

This example leverages the [terraform/examples/daos_cluster](../daos_cluster) example to provision a DAOS cluster and configure the clients so that an [IO500 benchmark](https://github.com/IO500/io500) may be run.

If you have not done so already, please follow the instructions in the [Pre-Deployment Guide](../../../docs/pre-deployment_guide.md)

## Running the example

### Deploying the DAOS Cluster

```bash
cd terraform/examples/io500
./start.sh
```

`start.sh`  will use the default configuration [terraform/examples/io500/config/config.sh](config/config.sh) to deploy a DAOS cluster using the `terraform/examples/daos_cluster` example.

When the `start.sh` script finishes you can log into the first DAOS client instance.

```bash
./login
```

Once logged into the first DAOS client instance you can run the IO500 benchmark.

```bash
./run_io500-sc22.sh
```

### Destroying the DAOS Cluster

To destroy the DAOS instances

```bash
cd terraform/examples/io500
./stop.sh
```

## Configuration

By default the `start.sh` script sources the
[./config/config.sh](config/config.sh) configuration file.

The [config/config.sh](config/config.sh) file contains environment
variables that control how Terraform will deploy the DAOS cluster.

To deploy different configurations run

```bash
start.sh -c config/<config_file>
```
