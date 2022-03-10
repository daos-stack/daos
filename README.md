# [Distributed Asynchronous Object Storage (DAOS)](https://docs.daos.io/) on [Google Cloud Platform (GCP)](https://cloud.google.com/)

This repository contains scripts to deploy [DAOS](https://docs.daos.io/) on [GCP](https://cloud.google.com/).

It consists of the directories:
- [images](images) - which contains scripts to prepare DAOS images for GCP used by Terraform code
- [terraform](terraform) - which contains Terraform code used to deploy DAOS on GCP

## Dependencies

In order to build images and deploy DAOS instances in Google Cloud Compute Engine you must have the following software installed.

- [git](https://git-scm.com/)
- [Google Cloud SDK](https://cloud.google.com/sdk/docs/install)
- [Packer](https://www.packer.io/downloads)
- [Terraform](https://www.terraform.io/downloads)

## Development

See [Development](docs/development.md)

