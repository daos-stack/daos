# DAOS Cluster Example

This Terraform configuration demonstrates how to use the [DAOS Terraform Modules](../../modules) to deploy a DAOS cluster.

## Terraform Files

List of Terraform files in this example

| Filename                      | Description                                                                     |
| ----------------------------- | ------------------------------------------------------------------------------- |
| main.tf                       | Main Terrform configuration file containing resource definitions                |
| variables.tf                  | Variable definitions for variables used in main.tf                              |
| versions.tf                   | Provider definitions                                                            |
| terraform.tfvars.perf.example | Pre-Configured variables for a cluster that is optimized for performance                   |
| terraform.tfvars.tco.example  | Pre-Configured variables for a cluster that is optimized for lower cost of ownership |

## Deployment

To deploy this example, see the instructions in [Deploy the DAOS Cluster Example](../../../docs/deploy_daos_cluster_example.md).

As an alternative to viewing the instructions as a standalone document, you can view the document as an in-context tutorial in [Cloud Shell](https://cloud.google.com/shell) by clicking the button below.

[![Open in Cloud Shell](https://gstatic.com/cloudssh/images/open-btn.png)](https://ssh.cloud.google.com/cloudshell/open?cloudshell_git_repo=https://github.com/daos-stack/google-cloud-daos&cloudshell_git_branch=develop&shellonly=true&cloudshell_tutorial=docs/deploy_daos_cluster_example.md)


## Terraform Configuration Documentation

<!-- BEGINNING OF PRE-COMMIT-TERRAFORM DOCS HOOK -->
Copyright 2022 Intel Corporation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

## Requirements

| Name | Version |
|------|---------|
| <a name="requirement_terraform"></a> [terraform](#requirement\_terraform) | >= 0.14.5 |
| <a name="requirement_google"></a> [google](#requirement\_google) | >= 3.54.0 |

## Providers

No providers.

## Modules

| Name | Source | Version |
|------|--------|---------|
| <a name="module_daos_client"></a> [daos\_client](#module\_daos\_client) | ../../modules/daos_client | n/a |
| <a name="module_daos_server"></a> [daos\_server](#module\_daos\_server) | ../../modules/daos_server | n/a |

## Resources

No resources.

## Inputs

| Name | Description | Type | Default | Required |
|------|-------------|------|---------|:--------:|
| <a name="input_allow_insecure"></a> [allow\_insecure](#input\_allow\_insecure) | Sets the allow\_insecure setting in the transport\_config section of the daos\_*.yml files | `bool` | `false` | no |
| <a name="input_client_gvnic"></a> [client\_gvnic](#input\_client\_gvnic) | Use Google Virtual NIC (gVNIC) network interface on DAOS clients | `bool` | `false` | no |
| <a name="input_client_instance_base_name"></a> [client\_instance\_base\_name](#input\_client\_instance\_base\_name) | MIG instance base names to use | `string` | `"daos-client"` | no |
| <a name="input_client_labels"></a> [client\_labels](#input\_client\_labels) | Set of key/value label pairs to assign to daos-client instances | `any` | `{}` | no |
| <a name="input_client_machine_type"></a> [client\_machine\_type](#input\_client\_machine\_type) | GCP machine type. ie. c2-standard-16 | `string` | `"c2-standard-16"` | no |
| <a name="input_client_mig_name"></a> [client\_mig\_name](#input\_client\_mig\_name) | MIG name | `string` | `"daos-client"` | no |
| <a name="input_client_number_of_instances"></a> [client\_number\_of\_instances](#input\_client\_number\_of\_instances) | Number of daos clients to bring up | `number` | `16` | no |
| <a name="input_client_os_disk_size_gb"></a> [client\_os\_disk\_size\_gb](#input\_client\_os\_disk\_size\_gb) | OS disk size in GB | `number` | `20` | no |
| <a name="input_client_os_disk_type"></a> [client\_os\_disk\_type](#input\_client\_os\_disk\_type) | OS disk type ie. pd-ssd, pd-standard | `string` | `"pd-ssd"` | no |
| <a name="input_client_os_family"></a> [client\_os\_family](#input\_client\_os\_family) | OS GCP image family | `string` | `"daos-client-hpc-centos-7"` | no |
| <a name="input_client_os_project"></a> [client\_os\_project](#input\_client\_os\_project) | OS GCP image project name. Defaults to project\_id if null. | `string` | `null` | no |
| <a name="input_client_preemptible"></a> [client\_preemptible](#input\_client\_preemptible) | If preemptible instances | `string` | `false` | no |
| <a name="input_client_service_account"></a> [client\_service\_account](#input\_client\_service\_account) | Service account to attach to the instance. See https://www.terraform.io/docs/providers/google/r/compute_instance_template.html#service_account. | <pre>object({<br>    email  = string,<br>    scopes = set(string)<br>  })</pre> | <pre>{<br>  "email": null,<br>  "scopes": [<br>    "https://www.googleapis.com/auth/devstorage.read_only",<br>    "https://www.googleapis.com/auth/logging.write",<br>    "https://www.googleapis.com/auth/monitoring.write",<br>    "https://www.googleapis.com/auth/servicecontrol",<br>    "https://www.googleapis.com/auth/service.management.readonly",<br>    "https://www.googleapis.com/auth/trace.append",<br>    "https://www.googleapis.com/auth/cloud-platform"<br>  ]<br>}</pre> | no |
| <a name="input_client_template_name"></a> [client\_template\_name](#input\_client\_template\_name) | MIG template name | `string` | `"daos-client"` | no |
| <a name="input_network_name"></a> [network\_name](#input\_network\_name) | Name of the GCP network to use | `string` | `"default"` | no |
| <a name="input_project_id"></a> [project\_id](#input\_project\_id) | The GCP project to use | `string` | n/a | yes |
| <a name="input_region"></a> [region](#input\_region) | The GCP region to create and test resources in | `string` | n/a | yes |
| <a name="input_server_daos_crt_timeout"></a> [server\_daos\_crt\_timeout](#input\_server\_daos\_crt\_timeout) | crt\_timeout | `number` | `300` | no |
| <a name="input_server_daos_disk_count"></a> [server\_daos\_disk\_count](#input\_server\_daos\_disk\_count) | Number of local ssd's to use | `number` | `16` | no |
| <a name="input_server_daos_disk_type"></a> [server\_daos\_disk\_type](#input\_server\_daos\_disk\_type) | Daos disk type to use. For now only suported one is local-ssd | `string` | `"local-ssd"` | no |
| <a name="input_server_daos_scm_size"></a> [server\_daos\_scm\_size](#input\_server\_daos\_scm\_size) | scm\_size | `number` | `200` | no |
| <a name="input_server_gvnic"></a> [server\_gvnic](#input\_server\_gvnic) | Use Google Virtual NIC (gVNIC) network interface | `bool` | `false` | no |
| <a name="input_server_instance_base_name"></a> [server\_instance\_base\_name](#input\_server\_instance\_base\_name) | MIG instance base names to use | `string` | `"daos-server"` | no |
| <a name="input_server_labels"></a> [server\_labels](#input\_server\_labels) | Set of key/value label pairs to assign to daos-server instances | `any` | `{}` | no |
| <a name="input_server_machine_type"></a> [server\_machine\_type](#input\_server\_machine\_type) | GCP machine type. ie. e2-medium | `string` | `"n2-custom-36-215040"` | no |
| <a name="input_server_mig_name"></a> [server\_mig\_name](#input\_server\_mig\_name) | MIG name | `string` | `"daos-server"` | no |
| <a name="input_server_number_of_instances"></a> [server\_number\_of\_instances](#input\_server\_number\_of\_instances) | Number of daos servers to bring up | `number` | `4` | no |
| <a name="input_server_os_disk_size_gb"></a> [server\_os\_disk\_size\_gb](#input\_server\_os\_disk\_size\_gb) | OS disk size in GB | `number` | `20` | no |
| <a name="input_server_os_disk_type"></a> [server\_os\_disk\_type](#input\_server\_os\_disk\_type) | OS disk type ie. pd-ssd, pd-standard | `string` | `"pd-ssd"` | no |
| <a name="input_server_os_family"></a> [server\_os\_family](#input\_server\_os\_family) | OS GCP image family | `string` | `"daos-server-centos-7"` | no |
| <a name="input_server_os_project"></a> [server\_os\_project](#input\_server\_os\_project) | OS GCP image project name. Defaults to project\_id if null. | `string` | `null` | no |
| <a name="input_server_pools"></a> [server\_pools](#input\_server\_pools) | List of pools and containers to be created | <pre>list(object({<br>    name       = string<br>    size       = string<br>    tier_ratio = number<br>    user       = string<br>    group      = string<br>    acls       = list(string)<br>    properties = map(any)<br>    containers = list(object({<br>      name            = string<br>      type            = string<br>      user            = string<br>      group           = string<br>      acls            = list(string)<br>      properties      = map(any)<br>      user_attributes = map(any)<br>    }))<br>  }))</pre> | `[]` | no |
| <a name="input_server_preemptible"></a> [server\_preemptible](#input\_server\_preemptible) | If preemptible instances | `string` | `false` | no |
| <a name="input_server_service_account"></a> [server\_service\_account](#input\_server\_service\_account) | Service account to attach to the instance. See https://www.terraform.io/docs/providers/google/r/compute_instance_template.html#service_account. | <pre>object({<br>    email  = string,<br>    scopes = set(string)<br>  })</pre> | <pre>{<br>  "email": null,<br>  "scopes": [<br>    "https://www.googleapis.com/auth/devstorage.read_only",<br>    "https://www.googleapis.com/auth/logging.write",<br>    "https://www.googleapis.com/auth/monitoring.write",<br>    "https://www.googleapis.com/auth/servicecontrol",<br>    "https://www.googleapis.com/auth/service.management.readonly",<br>    "https://www.googleapis.com/auth/trace.append",<br>    "https://www.googleapis.com/auth/cloud-platform"<br>  ]<br>}</pre> | no |
| <a name="input_server_template_name"></a> [server\_template\_name](#input\_server\_template\_name) | MIG template name | `string` | `"daos-server"` | no |
| <a name="input_subnetwork_name"></a> [subnetwork\_name](#input\_subnetwork\_name) | Name of the GCP sub-network to use | `string` | `"default"` | no |
| <a name="input_subnetwork_project"></a> [subnetwork\_project](#input\_subnetwork\_project) | The GCP project where the subnetwork is defined | `string` | `null` | no |
| <a name="input_zone"></a> [zone](#input\_zone) | The GCP zone to create and test resources in | `string` | n/a | yes |

## Outputs

No outputs.
<!-- END OF PRE-COMMIT-TERRAFORM DOCS HOOK -->
