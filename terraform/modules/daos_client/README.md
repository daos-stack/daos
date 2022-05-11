# DAOS Client module

This module creates a managed instance group of DAOS clients on GCP.

## Usage

The resources/services/activations/deletions that this module will create/trigger are:
- Create an instance tempate for DAOS clients
- Create a stateful instance group for DAOS clients

<!-- BEGINNING OF PRE-COMMIT-TERRAFORM DOCS HOOK -->
Copyright 2021 Google LLC

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
| <a name="requirement_google-beta"></a> [google-beta](#requirement\_google-beta) | >= 4.16.0 |

## Providers

| Name | Version |
|------|---------|
| <a name="provider_google"></a> [google](#provider\_google) | >= 3.54.0 |
| <a name="provider_google-beta"></a> [google-beta](#provider\_google-beta) | >= 4.16.0 |

## Modules

No modules.

## Resources

| Name | Type |
|------|------|
| [google-beta_google_compute_instance_template.daos_sig_template](https://registry.terraform.io/providers/hashicorp/google-beta/latest/docs/resources/google_compute_instance_template) | resource |
| [google_compute_instance_group_manager.daos_sig](https://registry.terraform.io/providers/hashicorp/google/latest/docs/resources/compute_instance_group_manager) | resource |
| [google_compute_per_instance_config.named_instances](https://registry.terraform.io/providers/hashicorp/google/latest/docs/resources/compute_per_instance_config) | resource |
| [google_compute_image.os_image](https://registry.terraform.io/providers/hashicorp/google/latest/docs/data-sources/compute_image) | data source |

## Inputs

| Name | Description | Type | Default | Required |
|------|-------------|------|---------|:--------:|
| <a name="input_allow_insecure"></a> [allow\_insecure](#input\_allow\_insecure) | Sets the allow\_insecure setting in the transport\_config section of the daos\_*.yml files | `bool` | `false` | no |
| <a name="input_certs_install_script_content"></a> [certs\_install\_script\_content](#input\_certs\_install\_script\_content) | Script to install certificates for DAOS secure transport | `string` | n/a | yes |
| <a name="input_daos_agent_yml"></a> [daos\_agent\_yml](#input\_daos\_agent\_yml) | YAML to configure the daos agent. | `string` | n/a | yes |
| <a name="input_daos_control_yml"></a> [daos\_control\_yml](#input\_daos\_control\_yml) | YAML configuring DAOS control. | `string` | n/a | yes |
| <a name="input_gvnic"></a> [gvnic](#input\_gvnic) | Use Google Virtual NIC (gVNIC) network interface | `bool` | `false` | no |
| <a name="input_instance_base_name"></a> [instance\_base\_name](#input\_instance\_base\_name) | MIG instance base names to use | `string` | `"daos-client"` | no |
| <a name="input_labels"></a> [labels](#input\_labels) | Set of key/value label pairs to assign to daos-client instances | `any` | `{}` | no |
| <a name="input_machine_type"></a> [machine\_type](#input\_machine\_type) | GCP machine type. ie. c2-standard-16 | `string` | `"c2-standard-16"` | no |
| <a name="input_mig_name"></a> [mig\_name](#input\_mig\_name) | MIG name | `string` | `"daos-client"` | no |
| <a name="input_network_name"></a> [network\_name](#input\_network\_name) | Name of the GCP network to use | `string` | `"default"` | no |
| <a name="input_number_of_instances"></a> [number\_of\_instances](#input\_number\_of\_instances) | Number of daos clients to bring up | `number` | `4` | no |
| <a name="input_os_disk_size_gb"></a> [os\_disk\_size\_gb](#input\_os\_disk\_size\_gb) | OS disk size in GB | `number` | `20` | no |
| <a name="input_os_disk_type"></a> [os\_disk\_type](#input\_os\_disk\_type) | OS disk type ie. pd-ssd, pd-standard | `string` | `"pd-ssd"` | no |
| <a name="input_os_family"></a> [os\_family](#input\_os\_family) | OS GCP image family | `string` | `"daos-client-hpc-centos-7"` | no |
| <a name="input_os_project"></a> [os\_project](#input\_os\_project) | OS GCP image project name. Defaults to project\_id if null. | `string` | `null` | no |
| <a name="input_preemptible"></a> [preemptible](#input\_preemptible) | If preemptible instances | `string` | `false` | no |
| <a name="input_project_id"></a> [project\_id](#input\_project\_id) | The GCP project to use | `string` | n/a | yes |
| <a name="input_region"></a> [region](#input\_region) | The GCP region to create and test resources in | `string` | n/a | yes |
| <a name="input_service_account"></a> [service\_account](#input\_service\_account) | Service account to attach to the instance. See https://www.terraform.io/docs/providers/google/r/compute_instance_template.html#service_account. | <pre>object({<br>    email  = string,<br>    scopes = set(string)<br>  })</pre> | <pre>{<br>  "email": null,<br>  "scopes": [<br>    "https://www.googleapis.com/auth/devstorage.read_only",<br>    "https://www.googleapis.com/auth/logging.write",<br>    "https://www.googleapis.com/auth/monitoring.write",<br>    "https://www.googleapis.com/auth/servicecontrol",<br>    "https://www.googleapis.com/auth/service.management.readonly",<br>    "https://www.googleapis.com/auth/trace.append",<br>    "https://www.googleapis.com/auth/cloud-platform"<br>  ]<br>}</pre> | no |
| <a name="input_subnetwork_name"></a> [subnetwork\_name](#input\_subnetwork\_name) | Name of the GCP sub-network to use | `string` | `"default"` | no |
| <a name="input_subnetwork_project"></a> [subnetwork\_project](#input\_subnetwork\_project) | The GCP project where the subnetwork is defined | `string` | `null` | no |
| <a name="input_template_name"></a> [template\_name](#input\_template\_name) | MIG template name | `string` | `"daos-client"` | no |
| <a name="input_zone"></a> [zone](#input\_zone) | The GCP zone to create and test resources in | `string` | n/a | yes |

## Outputs

No outputs.
<!-- END OF PRE-COMMIT-TERRAFORM DOCS HOOK -->
