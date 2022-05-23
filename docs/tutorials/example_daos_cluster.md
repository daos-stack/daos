# DAOS Cluster Example

## Overview

In this tutorial you will

1. Use Terraform to deploy a DAOS cluster using the example configuration in  ```terraform/examples/daos_cluster```
2. Perform the following DAOS administration tasks
   - Create a pool
   - Create a container
   - Mount the container
3. Copy files to DAOS mounted storage
4. Tear down the DAOS cluster deployment

Click on **Start**

## Pre-Deployment Steps

Before continuing, it is assumed that you have completed the pre-deployment steps in your GCP project.

If you are not sure if you have completed the pre-deployment steps run

```bash
gcloud compute images list --filter="name:daos" --format="value(name)"
```

If you see `daos-server-*` and `daos-client-*` images, click **Next** to continue to the next step.

Otherwise, run another tutorial that walks you though the steps listed above.

```bash
teachme docs/tutorials/pre-deployment.md
```

## The daos_cluster example

The [terraform/examples/daos_cluster](https://github.com/daos-stack/google-cloud-daos/tree/main/terraform/examples/daos_cluster) Terraform configuration demonstrates how the DAOS Terraform Modules in [terraform/modules](https://github.com/daos-stack/google-cloud-daos/tree/main/terraform/modules) can be used in your own Terraform configurations.

Change into the `daos_cluster` example directory now

```bash
cd terraform/examples/daos_cluster
```

This will be our working directory for the rest of the tutorial.

Click **Next** to continue

## Create a `terraform.tfvars` file

Create a `terraform.tfvars` file that contains variable values for Terraform.

To simplify the task of setting the proper variable values for a working DAOS cluster, there are example tfvars files that can be copied to create a  `terraform.tfvars` file.

You will need to select one of the example files to copy to `terraform.tfvars`.

The example tfvars files are

1. `terraform.tfvars.tco.example`

   - 16 DAOS Clients
   - 4 DAOS Servers with 16x375GB NVMe SSDs per server

   To use this configuration run
   ```bash
   cp terraform.tfvars.tco.example terraform.tfvars
   ```

2. `terraform.tfvars.perf.example`

   - 16 DAOS Clients
   - 4 DAOS Servers with 4x375GB NVMe SSDs per server

   To use this configuration run
   ```bash
   cp terraform.tfvars.perf.example terraform.tfvars
   ```

Click **Next** to continue

## Modify `terraform.tfvars`

Replace the `<project_id>` placeholder `terraform.tfvars` file by running

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

## Deploy the DAOS cluster

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

**List the instances**

Terraform will create 2 [managed instance groups (MIGs)](https://cloud.google.com/compute/docs/instance-groups) that will create the DAOS server and client instances.

It may take some time for the instances to become available.

To see the list of instances run

```bash
gcloud compute instances list \
  --filter="name ~ daos.*" \
  --format="value(name,INTERNAL_IP)"
```

Click **Next** to continue

## Log Into First Client

Log into the first DAOS client instance

```bash
gcloud compute ssh daos-client-0001
```

If you are prompted to create an SSH key pair for gcloud, follow the prompts.

Click **Next** to continue

## Create a Pool

The DAOS Management Tool `dmg` is meant to be used by administrators to manage the DAOS storage system and pools.

You will need to run `dmg` with `sudo`.

Check to make sure that the DAOS storage system is ready

```bash
sudo dmg system query -v
```

You should see 4 servers with a state of *Joined*

<br>

View free NVMe storage

```bash
sudo dmg storage query usage
```

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

Create a pool named `pool1` that uses all available space.

```bash
sudo dmg pool create -z 6.4TB -t 3 --label=pool1
```

View the ACLs on *pool1*

```bash
sudo dmg pool get-acl pool1
```

```text
# Owner: root@
# Owner Group: root@
# Entries:
A::OWNER@:rw
A:G:GROUP@:rw
```

Here we see that root owns the pool.

Add an [ACE](https://docs.daos.io/v2.0/admin/pool_operations/#adding-and-updating-aces) that will allow any user to create a container in the pool

```bash
sudo dmg pool update-acl -e A::EVERYONE@:rcta pool1
```

Click **Next** to continue

### Create a Container

Create a container named `cont1` in the `pool1` pool.

```bash
daos container create \
  --type=POSIX \
  --properties=rf:0 \
  --label=cont1 pool1
```

Click **Next** to continue

## Mount the Container

Create a mount point and mount the `cont1` container

```bash
MOUNT_DIR="${HOME}/daos/cont1"
mkdir -p "${MOUNT_DIR}"
dfuse --singlethread \
  --pool=pool1 \
  --container=cont1 \
  --mountpoint="${MOUNT_DIR}"
```

Verify that the container is mounted

```bash
df -h -t fuse.daos
```

Click **Next** to continue

## Use DAOS Storage

The `cont1` container is now mounted on `${HOME}/daos/cont1`

Create a 20GiB file which will be stored in the DAOS filesystem.

```bash
pushd ${HOME}/daos/cont1
time LD_PRELOAD=/usr/lib64/libioil.so \
dd if=/dev/zero of=./test20GiB.img iflag=fullblock bs=1G count=20
```

Click **Next** to continue

## Unmount the Container

Unmount the container before logging out of the daos-client-0001 instance.

```bash
popd
fusermount -u ${HOME}/daos/cont1
logout
```

Click **Next** to continue

## Shut Down the DAOS Cluster

Destroy all resources created by Terraform

```bash
terraform destroy
```

Click **Next** to continue

## Congratulations!

<walkthrough-conclusion-trophy></walkthrough-conclusion-trophy>

You have completed a DAOS cluster deployment on GCP!

The following steps were performed in this tutorial:

1. Used the Terraform example configuration in `terraform/examples/daos_cluster` to deploy a DAOS cluster.
2. Created a container
3. Mounted the container
3. Stored a large file in the container
4. Unmounted the container
5. Used terraform to destroy all resources that were created


What's next?

See [https://docs.daos.io](https://docs.daos.io) to learn more about DAOS!
