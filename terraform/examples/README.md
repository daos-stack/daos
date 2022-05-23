# Terraform Examples

This directory includes examples of Terraform configurations for different types of [DAOS](https://docs.daos.io/) deployments in GCP.

| Subdirectory                 | Description                                                                                                                                                                           |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [daos_cluster](daos_cluster) | Example Terraform configuration for a DAOS cluster consisting of servers and clients                                                                                                  |
| [io500](io500)               | Example that uses custom client images that have [IO500](https://github.com/IO500/io500) pre-installed. Uses the daos_cluster example to deploy a DAOS cluster with the IO500 images. |
