# DAOS on GCP

Distributed Asynchronous Object Storage ([DAOS](https://docs.daos.io/)) on Google Cloud Platform ([GCP](https://cloud.google.com/))

This repository contains:

- [Packer](https://www.packer.io/) scripts used to build DAOS images with [Google Cloud Build](https://cloud.google.com/build)
- [Terraform](https://www.terraform.io/) modules that can be used to deploy DAOS Server and Client instances
- [Terraform](https://www.terraform.io/) examples that demonstrate how to use the Terraform modules

**Directory structure**

```
.
├── docs            Miscellaneousc documentation and Cloud Shell tutorials
├── images          Cloud Build config files and Packer templates
│   └── scripts     Scripts that Packer runs to configure images
├── terraform       Terraform content
│   ├── examples    Examples that demonstrate how to use the DAOS Terraform modules
│   └── modules     Terraform modules for deploying DAOS server and client instances
└── tools           Tools used by pre-commit
```

### Prerequisites

In order to deploy DAOS on GCP you will need

- **Access to the Google Cloud Platform (GCP)**

   See [Get Started with Google Cloud](https://cloud.google.com/docs/get-started)

- **A GCP Project**

  See [Creating and managing projects](https://cloud.google.com/resource-manager/docs/creating-managing-projects)

- **Required Software**

  The documentation in this repository assumes that you will use [Cloud Shell](https://cloud.google.com/shell).

  If you use [Cloud Shell](https://cloud.google.com/shell), you do not need to install any software on your system.

  If you do not want to use Cloud Shell, you will need to install
    - [Git](https://git-scm.com/)
    - [Google Cloud CLI](https://cloud.google.com/sdk/docs/install)
    - [Terraform](https://learn.hashicorp.com/tutorials/terraform/install-cli)

### Deploy DAOS on GCP

Steps to deploy DAOS on GCP

1. **Set defaults for Google Cloud CLI (```gcloud```)**

   Only needs to be done once in your shell (Cloud Shell or local shell).

2. **Create a Packer image in your GCP project**

   In order to build DAOS images with Cloud Build your GCP project must contain a Packer image.

   Building the Packer images only needs to be done once for a GCP project.

3. **Build DAOS Server and Client images**

   DAOS Server and Client instances are deployed using images that have DAOS pre-installed.

   Therefore, the images need to be built prior to running Terraform.

  >  Click the button to open an interactive walk-through in Cloud Shell which will guide you through the steps listed above.
  >
  >  [![DAOS on GCP Setup](http://gstatic.com/cloudssh/images/open-btn.png)](https://console.cloud.google.com/cloudshell/open?git_repo=https://github.com/daos-stack/google-cloud-daos&cloudshell_git_branch=main&shellonly=true&tutorial=docs/tutorials/daosgcp_setup.md)

4. **Use DAOS Terraform modules in your Terraform code**

   You will need to write your own Terraform code for your particular use case.

   Your Terraform code can use the modules in ```terraform/modules``` to deploy DAOS server and client instances.

   The example Terraform configurations provided in ```terraform/examples``` can be used as a reference.

   See the [DAOS Cluster](terraform/examples/daos_cluster/README.md) example to learn more about how to use the ```terraform/modules```.

## Links

- [Distributed Asynchronous Object Storage (DAOS)](https://docs.daos.io/)
- [Google Cloud Platform (GCP)](https://cloud.google.com/)
- [Google Cloud CLI (gcloud)](https://cloud.google.com/cli)
- [Google Cloud Build](https://cloud.google.com/build)
- [Cloud Shell](https://cloud.google.com/shell)
- [Packer](https://www.packer.io/)
- [Terraform](https://www.terraform.io/)


## Development

If you are contributing to the code in this repo, see [Development](docs/development.md)

## License

[Apache License Version 2.0](LICENSE)
