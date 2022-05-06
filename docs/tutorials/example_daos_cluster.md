# DAOS GCP Full Cluster Example

## Overview

In this tutorial you will

1. Use Terraform to deploy a DAOS cluster using the example configuration in  ```terraform/examples/daos_cluster```
2. Perform the following DAOS administration tasks
   - Format Storage
   - Create a Pool
   - Create a Container
   - Mount the storage on the clients
3. Copy files to DAOS mounted storage
4. Tear down the DAOS cluster deployment

Click on **Start**

## Setup and Requirements

Before continuing, it is assumed that you have completed the following steps in your GCP project.

1. Set defaults for Google Cloud CLI (```gcloud```) in Cloud Shell
2. Create a Packer image in your GCP project
3. Build DAOS Server and Client images with Packer in Cloud Build

If you have not yet completed these steps, you can open a tutorial in Cloud Shell that will guide you through each step.

If you are not sure if you have completed these steps run

```bash
gcloud compute images list --filter="name:daos" --format="value(name)"
```

If you see `daos-server-*` and `daos-client-*` images, click **Next** to continue to the next step.

Otherwise, run another tutorial that walks you though the steps listed above.

```bash
teachme docs/tutorials/daosgcp_setup.md
```

## The daos_cluster example

The example Terraform configuration in [terraform/examples/daos_cluster](https://github.com/daos-stack/google-cloud-daos/tree/main/terraform/examples/daos_cluster) demonstrates how the [DAOS Terraform Modules](https://github.com/daos-stack/google-cloud-daos/tree/main/terraform/modules) can be used in your own Terraform code.

Change into the example directory now

```bash
cd terraform/examples/daos_cluster
```

This will be our working directory for the rest of the tutorial.

Click **Next** to continue

## Create a `terraform.tfvars` file

You need to create a `terraform.tfvars` file that contains variable values for Terraform.

There are many variables to configure DAOS server and client configurations. Changes to certain variable values often require corresponding changes in other variable values. Depending on your use case this can become a complex topic.

To simplify the task of setting the proper variable values for a working DAOS cluster, there are example tfvars files that can be copied to create a  `terraform.tfvars` file.

Select one of the example files to copy to `terraform.tfvars`.

The example tfvars files are:

1. `terraform.tfvars.tco.example`

   16 DAOS Clients, 4 DAOS Servers with 16 375GB NVMe SSDs per server.

   To use this configuration run
   ```bash
   cp terraform.tfvars.tco.example terraform.tfvars
   ```

2. `terraform.tfvars.perf.example`

   16 DAOS Clients, 4 DAOS Servers with 4 375GB NVMe SSDs per server.

   To use this configuration run
   ```bash
   cp terraform.tfvars.perf.example terraform.tfvars
   ```

Click **Next** to continue

## Modify `terraform.tfvars`

Now that you have created a `terraform.tfvars` file, there is one change that needs to be made in the file.

You need to replace the `<project_id>` placeholder with your project id.

To replace the `<project_id>` placeholder run

```bash
PROJECT_ID=$(gcloud config list --format 'value(core.project)')
sed -i "s/<project_id>/${PROJECT_ID}/g" terraform.tfvars
```

To view the `terraform.tfvars` file in the Cloud Shell editor run

```bash
cloudshell edit terraform.tfvars
```

Notice that the `terraform.tfvars` file contains the values for variables that are defined in `variables.tf`

```bash
cloudshell edit variables.tf
```

Click **Next** to continue

## View `main.tf`

Open the `main.tf` file

```bash
cloudshell edit main.tf
```

Notice how the `main.tf` uses the modules in [terraform/modules](https://github.com/daos-stack/google-cloud-daos/tree/main/terraform/modules).

The `terraform/examples/daos_cluster` Terraform configuration only needed to define the variables that are passed to the `daos_server` and `daos_client` modules in `main.tf`.

The variable definitions are in `variables.tf`.

The variable values are set in `terrafrom.tfvars`.

Click **Next** to continue

## Run Terraform to Deploy the DAOS cluster

You can now deploy a DAOS cluster using the `terraform/examples/daos_cluster` example configuration.

Initialize the working directory.

```bash
terraform init -input=false
```

Create an execution plan.

```bash
terraform plan -out=tfplan -input=false
```

Execute the actions in the plan.

```bash
terraform apply -input=false tfplan
```

List the instances that were created.

Terraform will create 2 [managed instance groups (MIGs)](https://cloud.google.com/compute/docs/instance-groups) that will create the DAOS server and client instances.

It may take some time for the instances to become available.

To see the list of instances run

```bash
gcloud compute instances list --filter="name ~ daos.*" --format="value(name,INTERNAL_IP)"
```

Click **Next** to continue

## Prepare storage

When the DAOS server and client instances are deployed the DAOS services are started but the DAOS storage is not ready to use yet.

There are a few administrative tasks that must be performed before the DAOS storage can be used.

The DAOS Management Tool (`dmg`) is installed on all DAOS client instances and can be used to perform administrative tasks.

You can use `dmg` on any of the DAOS client instances.

Log into the first DAOS client instance

```bash
gcloud compute ssh daos-client-0001
```

If you are prompted to create an SSH key pair for gcloud, follow the prompts.

Click **Next** to continue

## Storage Format

When the DAOS server instances are created the `daos_server` service will be started but will be in "maintenance mode".

In order to begin using the storage you must issue a *format* command.

To format the storage run

```bash
sudo dmg storage format
sudo dmg system query -v
```

To learn more see [Storage Formatting](https://docs.daos.io/latest/admin/deployment/#storage-formatting)

Click **Next** to continue

## Create pool

Now that the system has been formatted you can create a Pool.

First check to see how much free NVMe storage you have.

```bash
sudo dmg storage query usage
```

This will return storage information for the servers.

The output looks similar to

```
Hosts            SCM-Total SCM-Free SCM-Used NVMe-Total NVMe-Free NVMe-Used
-----            --------- -------- -------- ---------- --------- ---------
daos-server-0001 48 GB     48 GB    0 %      1.6 TB     1.6 TB    0 %
daos-server-0002 48 GB     48 GB    0 %      1.6 TB     1.6 TB    0 %
daos-server-0003 48 GB     48 GB    0 %      1.6 TB     1.6 TB    0 %
daos-server-0004 48 GB     48 GB    0 %      1.6 TB     1.6 TB    0 %
```


In the example output above there are 4 servers with a total of 6.4TB of free space.

With that information you know you can safely create a 6TB pool.

Create the pool.

```bash
sudo dmg pool create -z 6TB -t 3 -u ${USER} --label=daos_pool
```

For more information about pools see

- [https://docs.daos.io/latest/overview/storage/#daos-pool](https://docs.daos.io/latest/overview/storage/#daos-pool)
- [https://docs.daos.io/latest/admin/pool_operations/](https://docs.daos.io/latest/admin/pool_operations/)

Click **Next** to continue

### Create container

Now that a pool has been created, create a container in that pool

```bash
daos container create --type=POSIX --properties=rf:0 --label=daos_cont daos_pool
```

For more information about containers see [https://docs.daos.io/latest/overview/storage/#daos-container](https://docs.daos.io/latest/overview/storage/#daos-container)

Click **Next** to continue

## Mount container

The container now needs to be mounted.

To mount the container run

```bash
MOUNT_DIR="/tmp/daos_test1"
mkdir -p "${MOUNT_DIR}"
dfuse --singlethread --pool=daos_pool --container=daos_cont --mountpoint="${MOUNT_DIR}"
df -h -t fuse.daos
```

Your DAOS storage is now ready!

You can now store files in `/tmp/daos_test1`

Click **Next** to continue

## Shutting it down

If you are still logged into the first DAOS client instance, log out now.

To shut down the DAOS cluster run

```bash
terraform destroy
```

Click **Next** to continue

## Congratulations!

<walkthrough-conclusion-trophy></walkthrough-conclusion-trophy>

You have completed a DAOS cluster deployment on GCP!

In this tutorial you used the Terraform example configuration in `terraform/examples/daos_cluster` to deploy a DAOS cluster.

You then performed the following administration tasks:

1. Formatted storage
2. Created a pool
3. Created a container
4. Mounted the container

What's next?

See [https://docs.daos.io](https://docs.daos.io) to learn more about DAOS!
