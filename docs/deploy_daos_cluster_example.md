# Deploy the DAOS Cluster Example

## Overview

These instructions describe how to deploy a DAOS Cluster using the example in [terraform/examples/daos_cluster](../terraform/examples/daos_cluster).

Deployment tasks described in these instructions:

- Deploy a DAOS cluster using Terraform
- Perform DAOS administrative tasks to prepare the storage
- Mount a DAOS container with [DFuse (DAOS FUSE)](https://docs.daos.io/v2.0/user/filesystem/?h=dfuse#dfuse-daos-fuse)
- Store files in a DAOS container
- Unmount the container
- Remove the deployment (terraform destroy)

## Clone the repository

Clone the [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos) repository and change your working directory to the DAOS Cluster example directory.

```bash
cd ~/
git clone https://github.com/daos-stack/google-cloud-daos.git
cd ~/google-cloud-daos/terraform/examples/daos_cluster
```

## Create a `terraform.tfvars` file

Before you run `terraform` you need to create a `terraform.tfvars` file in the `terraform/examples/daos_cluster` directory.

The `terraform.tfvars` file contains the variable values for the configuration.

To ensure a successful deployment of a DAOS cluster there are two pre-configured `terraform.tfvars.*.example` files that you can choose from.

You will need to decide which of these files to copy to `terraform.tfvars`.

### The terraform.tfvars.tco.example file

The `terraform.tfvars.tco.example` contains variables for a DAOS cluster deployment with
- 16 DAOS Client instances
- 4 DAOS Server instances

  Each server instance has sixteen 375GB NVMe SSDs

To use the `terraform.tfvars.tco.example` file

```bash
cp terraform.tfvars.tco.example terraform.tfvars
```

### The terraform.tfvars.perf.example file

The `terraform.tfvars.perf.example` contains variables for a DAOS cluster deployment with
- 16 DAOS Client instances
- 4 DAOS Server instances

  Each server instances has four 375GB NVMe SSDs

To use the ```terraform.tfvars.perf.example``` file run

```bash
cp terraform.tfvars.perf.example terraform.tfvars
```

### Update variables in `terraform.tfvars`

Now that you have a `terraform.tfvars` file you need to replace the variable placeholders in the file with the values from your active `gcloud` configuration.

To update the variables in `terraform.tfvars` run

```bash
PROJECT_ID=$(gcloud config list --format 'value(core.project)')
REGION=$(gcloud config list --format 'value(compute.region)')
ZONE=$(gcloud config list --format 'value(compute.zone)')

sed -i "s/<project_id>/${PROJECT_ID}/g" terraform.tfvars
sed -i "s/<region>/${REGION}/g" terraform.tfvars
sed -i "s/<zone>/${ZONE}/g" terraform.tfvars
```

## Deploy the DAOS cluster

---

**Billing Notification!**

Running this example will incur charges in your project.

To avoid surprises, be sure to monitor your costs associated with running this example.

Don't forget to shut down the DAOS cluster with `terraform destroy` when you are finished.

---

### Deploy with Terraform

To deploy the DAOS cluster

```bash
terraform init
terraform plan -out=tfplan
terraform apply tfplan
```

### List the instances

Terraform will create 2 [Managed Instance Groups (MIGs)](https://cloud.google.com/compute/docs/instance-groups) that will create the DAOS server and client instances.

It may take some time for the instances to become available.

Verify that the daos-client and daos-server instances are running.

```bash
gcloud compute instances list \
  --filter="name ~ daos" \
  --format="value(name,INTERNAL_IP)"
```

Verify that the [MIGs](https://cloud.google.com/compute/docs/instance-groups) are stable.

```bash
PROJECT_ID=$(gcloud config list --format 'value(core.project)')
ZONE=$(gcloud config list --format 'value(compute.zone)')

echo "Checking daos-client MIG"
gcloud compute instance-groups managed wait-until 'daos-client' \
    --stable \
    --project="${PROJECT_ID}" \
    --zone="${ZONE}"

echo "Checking daos-server MIG"
gcloud compute instance-groups managed wait-until 'daos-server' \
    --stable \
    --project="${PROJECT_ID}" \
    --zone="${ZONE}"
```

## Perform DAOS administration tasks

After your DAOS cluster has been deployed you can log into the first DAOS server instance to perform administrative tasks.

### Log into the first DAOS server instance

Log into the first server instance

```bash
gcloud compute ssh daos-server-0001
```

### Verify that all daos-server instances have joined

The DAOS Management Tool `dmg` is meant to be used by administrators to manage the DAOS storage system and pools.

You will need to run `dmg` with `sudo`.

Use `dmg` to verify that the DAOS storage system is ready.

```bash
sudo dmg system query -v
```

The *State* column should display "Joined" for all servers.

```
Rank UUID                                 Control Address   Fault Domain      State  Reason
---- ----                                 ---------------   ------------      -----  ------
0    0796c576-5651-4e37-aa15-09f333d2d2b8 10.128.0.35:10001 /daos-server-0001 Joined
1    f29f7058-8abb-429f-9fd3-8b13272d7de0 10.128.0.77:10001 /daos-server-0003 Joined
2    09fc0dab-c238-4090-b3f8-da2bd4dce108 10.128.0.81:10001 /daos-server-0002 Joined
3    2cc9140b-fb12-4777-892e-7d190f6dfb0f 10.128.0.30:10001 /daos-server-0004 Joined
```

### Create a Pool

View the amount of free NVMe storage.

```bash
sudo dmg storage query usage
```

The output will look different depending on which `terraform.tfvars.*.example` file you copied to create the `terraform.tfvars` file.

The output will look similar to this

```
Hosts            SCM-Total SCM-Free SCM-Used NVMe-Total NVMe-Free NVMe-Used
-----            --------- -------- -------- ---------- --------- ---------
daos-server-0001 48 GB     48 GB    0 %      1.6 TB     1.6 TB    0 %
daos-server-0002 48 GB     48 GB    0 %      1.6 TB     1.6 TB    0 %
daos-server-0003 48 GB     48 GB    0 %      1.6 TB     1.6 TB    0 %
daos-server-0004 48 GB     48 GB    0 %      1.6 TB     1.6 TB    0 %
```

This shows how much NVMe-Free space is available for each server.

Create a pool named `pool1` that uses the total NVMe-Free for all servers.

```bash
TOTAL_NVME_FREE="$(sudo dmg storage query usage | awk '{split($0,a," "); sum += a[10]} END {print sum}')TB"
echo "Total NVMe-Free: ${TOTAL_NVME_FREE}"
sudo dmg pool create --size="${TOTAL_NVME_FREE}" --tier-ratio=3 --label=pool1
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

This completes the administration tasks for the pool.

For more information about pools see

- [Overview - Storage Model - DAOS Pool](https://docs.daos.io/latest/overview/storage/#daos-pool)
- [Administration Guide - Pool Operations](https://docs.daos.io/latest/admin/pool_operations/)

### Log out of the first server instance

Now that the administrative tasks have been completed, you may log out of the first server instance.

```bash
logout
```

## Create a Container

User tasks such as creating and mounting a container will be done on the first client

### Log into the first DAOS client instance

Log into the first client instance

```bash
gcloud compute ssh daos-client-0001
```


Create a [container](https://docs.daos.io/latest/overview/storage/#daos-container) in the pool

```bash
daos container create --type=POSIX --properties=rf:0 --label=cont1 pool1
```

For more information about containers see

- [Overview - Storage Model - DAOS Container](https://docs.daos.io/latest/overview/storage/#daos-container)
- [User Guide - Container Management](https://docs.daos.io/latest/user/container/?h=container)

## Mount the container

Mount the container with `dfuse`

```bash
MOUNT_DIR="${HOME}/daos/cont1"
mkdir -p "${MOUNT_DIR}"
dfuse --singlethread --pool=pool1 --container=cont1 --mountpoint="${MOUNT_DIR}"
df -h -t fuse.daos
```

You can now store files in the DAOS container mounted on `${HOME}/daos/cont1`.

For more information about DFuse see the [DAOS FUSE](https://docs.daos.io/latest/user/filesystem/?h=dfuse#dfuse-daos-fuse) section of the [User Guide](https://docs.daos.io/latest/user/workflow/).

## Use the Storage

The `cont1` container is now mounted on `${HOME}/daos/cont1`

Create a 20GiB file which will be stored in the DAOS filesystem.

```bash
cd ${HOME}/daos/cont1
time LD_PRELOAD=/usr/lib64/libioil.so \
  dd if=/dev/zero of=./test21G.img bs=1G count=20
```

## Unmount the container and logout of the first client

```bash
cd ~/
fusermount -u "${HOME}/daos/cont1"
logout
```

## Remove DAOS cluster deployment

To destroy the DAOS cluster run

```bash
terraform destroy
```

This will destroy all DAOS server and client instances.

## Congratulations!

You have successfully deployed a DAOS cluster using the [terraform/examples/daos_cluster](../terraform/examples/daos_cluster) example!
