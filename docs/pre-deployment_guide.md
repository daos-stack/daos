# Pre-Deployment Guide
## Overview

To deploy DAOS on GCP there are a few requirements.

  - You need a [Google Cloud](https://cloud.google.com/) account and a [project](https://cloud.google.com/resource-manager/docs/creating-managing-projects).
  - Your GCP project must have enough Compute Engine [quota](https://cloud.google.com/compute/quotas) to run the examples in this repository.
  - If you decide not to use Cloud Shell, you must have a Linux or macOS terminal with the required dependencies installed.
  - You must configure the [Google Cloud CLI (`gcloud`)](https://cloud.google.com/sdk/gcloud) with a default project, region and zone.
  - You must have a [Cloud NAT](https://cloud.google.com/nat/docs/overview).
  - You must build DAOS server and client images.

After completing this guide you will be ready to deploy DAOS.

## Create a GCP Project

When you create a Google Cloud account a project named "My First Project" will be created for you. The project will have a randomly generated ID.

Since *project name* and *project ID* are used in many configurations it is recommended that you create a new project specifically for your DAOS deployment or solution that will include DAOS.

To create a project, refer to the following documentation

- [Get Started with Google Cloud](https://cloud.google.com/docs/get-started)
- [Creating and managing projects](https://cloud.google.com/resource-manager/docs/creating-managing-projects)

Make note of the *Project Name* and *Project ID* for the project that you plan to use for your DAOS deployment as you will be using it later in various configurations.

---

**NOTE**

Some organizations require that GCP accounts and projects be created by a centralized IT department.

Depending on your organization you may need to make an internal request for access to GCP and ownership of a GCP project.

Often in these scenarios the projects have restrictions on service usage, networking, IAM, etc.in order to control costs and/or meet the security requirements of the organization. Such restrictions can sometimes result in failed deployments of DAOS.

If your project was created for you by your organization and you experience issues with the examples in this repo, it may be necessary to work with your organization to understand what changes can be made in your project to ensure a successful deployment of DAOS.

---

## Determine Region and Zone for Deployment

Determine the region and zone for your DAOS deployment.

See [Regions and Zones](https://cloud.google.com/compute/docs/regions-zones).

Make a note of your chosen region and zone as you will be using this information later.

## Terminal Selection and Software Installation

Decide which terminal you will use and start a session.

- **Cloud Shell**

  [Cloud Shell](https://cloud.google.com/shell) is an online development and operations environment accessible anywhere with your browser. You can manage your resources with its online terminal preloaded with utilities such as `git` and the `gcloud` command-line tool.

  With [Cloud Shell](https://cloud.google.com/shell) you do not need to install any software.

  Everything you need to deploy DAOS with the examples in this repository or with the [Cloud HPC Toolkit](https://cloud.google.com/hpc-toolkit) is already installed.

  Using [Cloud Shell](https://cloud.google.com/shell) is by far the easiest way to get started with DAOS on GCP.

  Depending on how you found this documentation you may already be viewing this content in a Cloud Shell tutorial. If so, you can click the next button at the bottom of the tutorial panel to continue.

  Otherwise, if you would like to open Cloud Shell in your browser, [click here](https://shell.cloud.google.com/?show=terminal&show=ide&environment_deployment=ide)

  ---

  **NOTE**

  Cloud Shell can run in Ephemeral Mode which does not persist storage. This has caused some confusion to some who are new to Cloud Shell since any changes made are not persisted across sessions. If you are running Cloud Shell in Ephemeral Mode be aware that any changes you make to files in your home directory will not be persisted. For more info, see  [Choose ephemeral mode](https://cloud.google.com/shell/docs/using-cloud-shell#choosing_ephemeral_mode).

  ---

- **Remote Cloud Shell**

  You may be thinking "I don't want to work in a browser!"

  With Cloud Shell you aren't forced to use a browser.

  If you [install the Google Cloud CLI](https://cloud.google.com/sdk/docs/install) on your system, you can use the [`gcloud cloud-shell ssh`](https://cloud.google.com/sdk/gcloud/reference/cloud-shell/ssh) command to launch an interactive Cloud Shell SSH session from your favorite terminal.

  This allows you to use your local terminal with the benefit of having the software dependencies already installed in Cloud Shell.

- **Local**

  Throughout the documentation in this repository, the term "local terminal" will refer to any terminal that is not Cloud Shell.

  The terminal may be on your system, a remote VM or bare metal machine, Docker container, etc.

  If you choose to use a *local* terminal, you will need to install the following dependencies.

  - [git](https://git-scm.com/book/en/v2/Getting-Started-Installing-Git)
  - [Terraform](https://learn.hashicorp.com/tutorials/terraform/install-cli)
  - [Google Cloud CLI](https://cloud.google.com/sdk/docs/install)

  If you plan to deploy DAOS with the Cloud HPC Toolkit, see the [Install dependencies](https://cloud.google.com/hpc-toolkit/docs/setup/install-dependencies) documentation for additional dependencies.

## Google Cloud CLI (`gcloud`) Configuration

Many of the bash scripts and Terraform configurations in this repository assume that you have set a default project, region and zone in your active `gcloud` configuration.

To configure `gcloud` run the following commands.

### Create a named configuration

Create a named configuration and make it the active config.

Replace `<config name>` with the name you would like to give your configuration.

```bash
gcloud config configurations create <config name> --activate
```

### Initialize the CLI

```bash
gcloud init --no-browser
```

Follow the instructions to Re-initialize the currently active configuration.

This will prompt you to set the default Project ID and User for the configuration.

### Set the default region

Replace `<region>` with the the name of the region you would like to use.

```bash
gcloud config set compute/region <region>
```

### Set the default zone

Replace `<zone>` with the the name of the region you would like to use.

```bash
gcloud config set compute/zone <zone>
```

### Verify the configuration defaults

```bash
gcloud config list
gcloud config configurations list --filter="IS_ACTIVE:True"
```

### Authorize the Google Cloud CLI

If you are currently in Cloud Shell, you don't need to run this command.

```bash
gcloud auth login
```

To learn more about using the Google Cloud CLI see the various [How-to Guides](https://cloud.google.com/sdk/docs/how-to).

## Quotas

Google Compute Engine enforces quotas on resources to prevent unforseen spikes in usage.

In order to deploy DAOS with the examples in this repository or the [community examples in the Google Cloud HPC Toolkit](https://github.com/GoogleCloudPlatform/hpc-toolkit/tree/main/community/examples/intel) you must have enough [quota](https://cloud.google.com/compute/quotas) for the region in which you are deploying.

Understanding the quota for a single DAOS server and client instance will allow you to calculate the quota needed to deploy DAOS clusters of varying sizes.

**Required quota for a single DAOS client instance**

```
Service             Quota                     Limit
------------------  ------------------------- ------
Compute Engine API  C2 CPUs                   16
Compute Engine API  Persistent Disk SSD (GB)  20GB
```

**Required quota for a single DAOS server instance**

```
Service             Quota                     Limit
------------------  ------------------------- ------
Compute Engine API  N2 CPUs                   36
Compute Engine API  Persistent Disk SSD (GB)  20GB
Compute Engine API  Local SSD (GB)            6TB
```

These quota limits are based on the machine types that are used in the examples as well as the maximum size and number of disks that can be attached to a server.

- DAOS Client: c2-standard-16 (16 vCPU, 64GB memory)
- DAOS Server: n2-custom-36-215040 (36 vCPU, 64GB memory)
- DAOS Server SSDs:
    Max number that can be attached to an instance = 16.
    Max size 375GB
    Quota Needed for 1 server: 16disks * 375GB = 6TB

So for the 4 server, 16 client examples in this repo you will need the following quotas

```
Service             Quota                     Limit  Description
------------------  ------------------------- ------ ------------------------------------------------------------------
Compute Engine API  C2 CPUs                   256    16 client instances * 16 = 64
Compute Engine API  N2 CPUs                   144    4 servers instances * 36 = 144
Compute Engine API  Persistent Disk SSD (GB)  400GB  (16 client instances * 20GB) + (4 server instances * 20GB) = 400GB
Compute Engine API  Local SSD (GB)            24TB   4 servers * (16 * 375GB disks) = 24TB
```

If your quotas do not have these minimum limits, you will need to [request an increase](https://cloud.google.com/compute/quotas#requesting_additional_quota) in order to deploy the examples in this repository.

To view your current quotas you can go to https://console.cloud.google.com/iam-admin/quotas

You can also run

```bash
REGION=$(gcloud config get-value compute/region)

gcloud compute regions describe "${REGION}"
```

For more information, see [Quotas and Limits](https://cloud.google.com/compute/quotas)

## Enable the default Compute Engine service account

The examples in this repository assume that you have enabled the default service account.

Enable the default Compute Engine service account.

```bash
PROJECT_ID=$(gcloud projects list --filter="$(gcloud config get-value project)" --format="value(PROJECT_ID)")

PROJECT_NUMBER=$(gcloud projects list --filter="$(gcloud config get-value project)" --format="value(PROJECT_NUMBER)")

gcloud iam service-accounts enable \
     --project="${PROJECT_ID}" \
     "${PROJECT_NUMBER}-compute@developer.gserviceaccount.com"
```

## Enable APIs

Enable the service APIs which are used in a DAOS deployment.

```bash
gcloud services enable cloudbuild.googleapis.com
gcloud services enable cloudresourcemanager.googleapis.com
gcloud services enable compute.googleapis.com
gcloud services enable iam.googleapis.com
gcloud services enable iap.googleapis.com
gcloud services enable networkmanagement.googleapis.com
gcloud services enable secretmanager.googleapis.com
gcloud services enable servicemanagement.googleapis.com
gcloud services enable sourcerepo.googleapis.com
gcloud services enable storage-api.googleapis.com
```

## Create a Cloud NAT

When deploying DAOS server and client instances external IPs are not added to the instances.

The instances need to use services that are not accessible on the internal VPC default network as well as the YUM repos at https://packages.daos.io.

Therefore, it is necessary to create a [Cloud NAT using Cloud Router](https://cloud.google.com/architecture/building-internet-connectivity-for-private-vms#create_a_nat_configuration_using_cloud_router).

First check to see if you already have a Cloud NAT for your region.

```bash
REGION=$(gcloud config get-value compute/region)

gcloud compute routers list --filter="region:${REGION}" --format="csv[no-heading,separator=' '](name)"
```

If the command returns a value, then you do not need to run the following commands, otherwise run

```bash
REGION=$(gcloud config get-value compute/region)

# Create a Cloud Router instance
gcloud compute routers create "nat-router-${REGION}" \
  --network default \
  --region "${REGION}"

# Configure the router for Cloud NAT
gcloud compute routers nats create nat-config \
  --router-region "${REGION}" \
  --router "nat-router-${REGION}" \
  --nat-all-subnet-ip-ranges \
  --auto-allocate-nat-external-ips
```

## Create a Packer Image

DAOS images are built using [Packer](https://www.packer.io/) in [Cloud Build](https://cloud.google.com/build).

In order to build DAOS images, your GCP project must contain a Packer image (an image with Packer installed).

Cloud Build will

1. Deploy an instance from the Packer image
2. Copy the Packer templates and provisioning scripts from the `images` directory in this repository to the instance
3. Run Packer in the instance to create the DAOS images

The DAOS images will then exist in your project so that you can deploy DAOS servers and clients.

### Identity-Aware Proxy (IAP) TCP forwarding

When Cloud Build creates an instance to configure for DAOS images it doesn't assign an external IP address. Cloud Build will use *Identity-Aware Proxy (IAP) TCP forwarding* to run commands on the instance.

In order for IAP TCP Forwarding to work, you need to create a firewall rule

```bash
gcloud compute firewall-rules create allow-ssh-ingress-from-iap \
  --direction=INGRESS \
  --action=allow \
  --rules=tcp:22 \
  --source-ranges=35.235.240.0/20
```

The IP range 35.235.240.0/20 contains all IP addresses that IAP uses for TCP forwarding.

For more information, see [Using IAP for TCP forwarding](https://cloud.google.com/iap/docs/using-tcp-forwarding#gcloud)

### Cloud Build Service Account

Grant the necessary roles to the Cloud Build service account

```bash
PROJECT_ID=$(gcloud projects list --filter="$(gcloud config get-value project)" --format="value(PROJECT_ID)")

CLOUD_BUILD_ACCOUNT=$(gcloud projects get-iam-policy ${PROJECT_ID}  --filter="(bindings.role:roles/cloudbuild.builds.builder)" --flatten="bindings[].members" --format="value(bindings.members[])")

gcloud projects add-iam-policy-binding "${PROJECT_ID}" \
  --member "${CLOUD_BUILD_ACCOUNT}" \
  --role roles/compute.instanceAdmin

gcloud projects add-iam-policy-binding "${PROJECT_ID}"  --member "${CLOUD_BUILD_ACCOUNT}" --role=roles/compute.instanceAdmin.v1

gcloud projects add-iam-policy-binding "${PROJECT_ID}"  --member "${CLOUD_BUILD_ACCOUNT}" --role=roles/secretmanager.admin

gcloud projects add-iam-policy-binding "${PROJECT_ID}"  --member "${CLOUD_BUILD_ACCOUNT}" --role=roles/iap.tunnelResourceAccessor

```

### Build the Packer image

```bash
pushd ~/
git clone https://github.com/GoogleCloudPlatform/cloud-builders-community.git
cd cloud-builders-community/packer
gcloud builds submit .
rm -rf ~/cloud-builders-community
popd
```

## Build DAOS Images

Build the DAOS Server and Client images

```bash
pushd images
./build_images.sh --type all
popd
```

## Congratulations!

You have completed the **Pre-Deployment** steps!

You are now ready to deploy DAOS on GCP.

Refer to the [Deployment section of the main README](https://github.com/markaolson/google-cloud-daos/tree/DAOSGCP-119#deployment) for information on how to deploy DAOS on GCP.
