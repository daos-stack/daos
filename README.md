# DAOS on GCP

Distributed Asynchronous Object Storage ([DAOS](https://docs.daos.io/)) on Google Cloud Platform ([GCP](https://cloud.google.com/))

This repository contains:

- [Packer](https://www.packer.io/) scripts used to build DAOS images with [Google Cloud Build](https://cloud.google.com/build)
- [Terraform](https://www.terraform.io/) modules that can be used to deploy DAOS Server and Client instances
- [Terraform](https://www.terraform.io/) examples that demonstrate how to use the Terraform modules

**Directory structure**

```
.
├── docs            Documentation and Cloud Shell tutorials
├── images          Cloud Build config files and Packer templates
│   └── scripts     Scripts that Packer runs to build images
├── terraform       Terraform content
│   ├── examples    Examples that demonstrate how to use the DAOS Terraform modules
│   └── modules     Terraform modules for deploying DAOS server and client instances
└── tools           Tools used by pre-commit
```

## Prerequisites

In order to deploy DAOS on GCP you will need

- **Access to the Google Cloud Platform (GCP)**

   See [Get Started with Google Cloud](https://cloud.google.com/docs/get-started)

- **A GCP Project**

  See [Creating and managing projects](https://cloud.google.com/resource-manager/docs/creating-managing-projects)

- **Required Software**

  The documentation in this repository assumes that you will use [Cloud Shell](https://cloud.google.com/shell).

  With [Cloud Shell](https://cloud.google.com/shell), there is no need to install any software on your system.

  If you do not want to use Cloud Shell, you will need to install
    - [Git](https://git-scm.com/)
    - [Google Cloud CLI](https://cloud.google.com/sdk/docs/install)
    - [Terraform](https://learn.hashicorp.com/tutorials/terraform/install-cli)

## Deploying DAOS on GCP

### Pre-Deployment Steps

The following pre-deployment steps are required

1. **Set defaults for Google Cloud CLI (```gcloud```)**

   Only needs to be done once in your shell (Cloud Shell or local shell).

2. **Enable service APIs and grant permissions**

   Enabling APIs and granting service account permissions only needs to be done once for a GCP project.

3. **Create a Packer image in your GCP project**

   In order to build DAOS images with Cloud Build your GCP project must contain a Packer image.

   Building the Packer image only needs to be done once for a GCP project.

4. **Build DAOS Server and Client images**

   DAOS Server and Client instances are deployed using images that have DAOS pre-installed.

   Therefore, the images need to be built prior to running Terraform to deploy a DAOS cluster.

Click the button below to open a Cloud Shell tutorial which will guide you through the pre-deployment steps listed above. If you lose your Cloud Shell session you can always come back to this README and click the button again.

[![DAOS on GCP Pre-Deployment](http://gstatic.com/cloudssh/images/open-btn.png)](https://console.cloud.google.com/cloudshell/open?git_repo=https://github.com/daos-stack/google-cloud-daos&cloudshell_git_branch=main&shellonly=true&tutorial=docs/tutorials/pre-deployment.md)

### Deploy a DAOS Cluster with Terraform

After completing the pre-deployment steps listed above, you will need to write your own Terraform configuration for your particular use case.

The [terraform/modules](terraform/modules) in this repo can be used in your Terraform configuration to deploy DAOS server and client instances.

The [terraform/examples/daos_cluster](terraform/examples/daos_cluster/README.md) example serves as both a reference and a quick way to deploy a DAOS cluster.

Click the button below to open a Cloud Shell tutorial that will walk you through using the [terraform/examples/daos_cluster](terraform/examples/daos_cluster/README.md) example to deploy a DAOS cluster.

[![DAOS Cluster Example](http://gstatic.com/cloudssh/images/open-btn.png)](https://console.cloud.google.com/cloudshell/open?git_repo=https://github.com/daos-stack/google-cloud-daos&cloudshell_git_branch=main&shellonly=true&tutorial=docs/tutorials/example_daos_cluster.md)

### Deploy a DAOS Cluster with the Google HPC Toolkit

The [HPC Toolkit](https://github.com/GoogleCloudPlatform/hpc-toolkit) is an open-source software offered by Google Cloud which makes it easy for customers to deploy HPC environments on Google Cloud.

The HPC Toolkit allows customers to deploy turnkey HPC environments (compute, networking, storage, etc) following Google Cloud best-practices, in a repeatable manner. It is designed to be highly customizable and extensible, and intends to address the HPC deployment needs of a broad range of customers.

The HPC Toolkit includes the following community examples which use the Terraform modules in this repository.

| HPC Toolkit Community Example | Description |
| ----------------------------- | ----------- |
| [DAOS Cluster](https://github.com/GoogleCloudPlatform/hpc-toolkit/tree/main/community/examples/intel#daos-cluster) | Use the HPC Toolkit to deploy a standalone DAOS cluster |
| [DAOS Server with Slurm cluster](https://github.com/GoogleCloudPlatform/hpc-toolkit/tree/main/community/examples/intel#daos-server-with-slurm-cluster) | Use the HPC Toolkit to deploy a set of DAOS servers for storage and a Slurm cluster in which the compute nodes are DAOS clients.  The example demonstrates how to use DAOS storage in a Slurm job. |

If you are just getting started with deploying DAOS on GCP, it is highly recommended to use the HPC Toolkit as it can save you a lot of time as opposed to developing your own Terraform configuration.

## Support

Content in the [google-cloud-daos](https://github.com/daos-stack/google-cloud-daos) repository is licensed under the [Apache License Version 2.0](LICENSE) open-source license.

[DAOS](https://github.com/daos-stack/daos) is being distributed under the BSD-2-Clause-Patent open-source license.

Intel Corporation provides several ways for the users to get technical support:

1. Community support is available to everybody through Jira and via the DAOS channel for the Google Cloud users on Slack.

   To access Jira, please follow these steps:

   - Navigate to https://daosio.atlassian.net/jira/software/c/projects/DAOS/issues/

   - You will need to request access to DAOS Jira to be able to create and update tickets. An Atlassian account is required for this type of access. Read-only access is available without an account.
   - If you do not have an Atlassian account, follow the steps at https://support.atlassian.com/atlassian-account/docs/create-an-atlassian-account/ to create one.

   To access the Slack channel for DAOS on Google Cloud, please follow this link https://daos-stack.slack.com/archives/C03GLTLHA59

   > This type of support is provided on a best-effort basis, and it does not have any SLA attached.

2. Commercial L3 support is available on an on-demand basis. Please get in touch with Intel Corporation to obtain more information.

   - You may inquire about the L3 support via the Slack channel (https://daos-stack.slack.com/archives/C03GLTLHA59)

## Links

- [Distributed Asynchronous Object Storage (DAOS)](https://docs.daos.io/)
- [Google Cloud Platform (GCP)](https://cloud.google.com/)
- [Google HPC Toolkit](https://github.com/GoogleCloudPlatform/hpc-toolkit)
- [Google Cloud CLI (gcloud)](https://cloud.google.com/cli)
- [Google Cloud Build](https://cloud.google.com/build)
- [Cloud Shell](https://cloud.google.com/shell)
- [Packer](https://www.packer.io/)
- [Terraform](https://www.terraform.io/)

## Development

If you are contributing to this repo, see [Development](docs/development.md)

## License

[Apache License Version 2.0](LICENSE)
