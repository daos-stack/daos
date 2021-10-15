# Terraform deployment of Distributed Asynchronous Object Storage (DAOS) on Google Cloud Platform (GCP)

This directory contains Terraform code to deploy DAOS on GCP.

This module consists of a collection of Terraform submodules to deploy DAOS client and server instances on GCP.
Below is the list of available submodules:

* [DAOS Server](modules/daos_server)
* [DAOS Client](modules/daos_client)

To deploy full DAOS cluster use [full_cluster_setup](examples/full_cluster_setup) example.

## Compatibility

This module is meant to use with Terraform 0.14.

## Examples

[examples](examples) directory contains Terraform code of how to use these particular submodules.
