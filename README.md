# DAOS on GCP

Distributed Asynchronous Object Storage ([DAOS](https://docs.daos.io/)) on Google Cloud Platform ([GCP](https://cloud.google.com/))

This repository contains:

- [Terraform](https://www.terraform.io/) modules for deploying DAOS Server and Client instances on GCP
- Scripts used to build DAOS images with [Cloud Build](https://cloud.google.com/build) and [Packer](https://www.packer.io/)
- Examples that demonstrate how to use the DAOS Terraform modules
- Documentation for deploying DAOS on GCP

## Pre-Deployment

In order to deploy DAOS on GCP there are several steps that are required to prepare your project.

For instructions, see the [Pre-Deployment Guide](docs/pre-deployment_guide.md)

As an alternative to viewing the guide as a standalone document, you can view it as an in-context tutorial in [Cloud Shell](https://cloud.google.com/shell) by clicking the button below.

[![Open in Cloud Shell](https://gstatic.com/cloudssh/images/open-btn.png)](https://ssh.cloud.google.com/cloudshell/open?cloudshell_git_repo=https://github.com/daos-stack/google-cloud-daos.git&cloudshell_git_branch=main&shellonly=true&cloudshell_tutorial=docs/pre-deployment_guide.md)



## Deployment

After completing the steps in the [Pre-Deployment Guide](docs/pre-deployment_guide.md) you will be ready to deploy DAOS.

To deploy DAOS on GCP you may choose one of the following deployment paths.

1. **Cloud HPC Toolkit**

   [Cloud HPC Toolkit](https://cloud.google.com/hpc-toolkit) is open-source software offered by Google Cloud which makes it easy for you to deploy high performance computing (HPC) environments. It is designed to be highly customizable and extensible, and intends to address the HPC deployment needs of a broad range of use cases.

   The [community examples](https://github.com/GoogleCloudPlatform/hpc-toolkit/tree/main/community/examples/intel) in the [Cloud HPC Toolkit](https://cloud.google.com/hpc-toolkit) use the [DAOS modules](terraform/modules/) in this repository.

   For documentation, see the [Cloud HPC Toolkit Overview](https://cloud.google.com/hpc-toolkit/docs/overview)

   **Prepare to use the Cloud HPC Toolkit**

   - **Dependencies**

      If you are using Cloud Shell, the dependencies are already installed.

      If you are not using Cloud Shell, you will need to install a few dependencies.

      See [Install dependencies](https://cloud.google.com/hpc-toolkit/docs/setup/install-dependencies)

   - **Configure Environment**

      Before you can deploy DAOS with the Cloud HPC Toolkit you will need to complete some configuration steps.

      See [Configure your environment](https://cloud.google.com/hpc-toolkit/docs/setup/configure-environment).

   **Deploy DAOS with the Cloud HPC Toolkit**

   For instructions on how to deploy the [community examples](https://github.com/GoogleCloudPlatform/hpc-toolkit/tree/main/community/examples/intel), see
   - [DAOS Cluster](https://github.com/GoogleCloudPlatform/hpc-toolkit/tree/main/community/examples/intel#daos-cluster)
   - [DAOS Server with Slurm Cluster](https://github.com/GoogleCloudPlatform/hpc-toolkit/tree/main/community/examples/intel#daos-server-with-slurm-cluster)

2. **Deploy DAOS with the DAOS cluster example**

   The [`terraform/examples/daos_cluster`](terraform/examples/daos_cluster/README.md) example demonstrates how to use the [DAOS modules](terraform/modules/) in a Terraform configuration to deploy a DAOS cluster consisting of servers and clients.

   To deploy the example, see [Deploy the DAOS Cluster Example](docs/deploy_daos_cluster_example.md).

   As an alternative to viewing the instructions in [Deploy the DAOS Cluster Example](docs/deploy_daos_cluster_example.md) as a standalone document, you can view it as an in-context tutorial in [Cloud Shell](https://cloud.google.com/shell) by clicking the button below.

   [![Open in Cloud Shell](https://gstatic.com/cloudssh/images/open-btn.png)](https://ssh.cloud.google.com/cloudshell/open?cloudshell_git_repo=https://github.com/daos-stack/google-cloud-daos.git&cloudshell_git_branch=main&shellonly=true&cloudshell_tutorial=docs/deploy_daos_cluster_example.md)

3. **Create your own Terraform Configurations**

   While this is the more complex deployment path, there may be reasons why this path is necessary.

   For example, you may want to add DAOS servers and clients to your existing Terraform configurations.

   When creating your own Terraform configurations refer to the documentation for the modules:
   - [daos_server module](terraform/modules/daos_server/README.md)
   - [daos_client module](terraform/modules/daos_client/README.md)

   You can also use the [`terraform/examples/daos_cluster`](terraform/examples/daos_cluster/README.md) example as a reference.

   Note that DAOS servers and clients should be deployed in the same Terraform configuration. This is because there are variables in the daos_client module that must get their values from the daos_server module.

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
- [Google Cloud HPC Toolkit](https://cloud.google.com/hpc-toolkit)
- [Google Cloud CLI (gcloud)](https://cloud.google.com/cli)
- [Google Cloud Build](https://cloud.google.com/build)
- [Cloud Shell](https://cloud.google.com/shell)
- [Packer](https://www.packer.io/)
- [Terraform](https://www.terraform.io/)

## Development

If you are contributing to this repo, see [Development](docs/development.md)

## License

[Apache License Version 2.0](LICENSE)
