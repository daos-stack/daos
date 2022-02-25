# DAOS Server module

This module creates a opinionated deployment of DAOS on GCP.

## Usage

The resources/services/activations/deletions that this module will create/trigger are:
- Create an instance tempate for DAOS servers
- Create a stateful instance group for DAOS servers

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

## Providers

| Name | Version |
|------|---------|
| <a name="provider_google"></a> [google](#provider\_google) | >= 3.54.0 |

## Modules

No modules.

## Resources

| Name | Type |
|------|------|
| [google_compute_instance_group_manager.daos_sig](https://registry.terraform.io/providers/hashicorp/google/latest/docs/resources/compute_instance_group_manager) | resource |
| [google_compute_instance_template.daos_sig_template](https://registry.terraform.io/providers/hashicorp/google/latest/docs/resources/compute_instance_template) | resource |
| [google_compute_per_instance_config.named_instances](https://registry.terraform.io/providers/hashicorp/google/latest/docs/resources/compute_per_instance_config) | resource |
| [google_compute_image.os_image](https://registry.terraform.io/providers/hashicorp/google/latest/docs/data-sources/compute_image) | data source |

## Inputs

| Name | Description | Type | Default | Required |
|------|-------------|------|---------|:--------:|
| <a name="input_daos_disk_count"></a> [daos\_disk\_count](#input\_daos\_disk\_count) | Number of local ssd's to use | `number` | `null` | no |
| <a name="input_daos_disk_type"></a> [daos\_disk\_type](#input\_daos\_disk\_type) | Daos disk type to use. For now only suported one is local-ssd | `string` | `"local-ssd"` | no |
| <a name="input_daos_service_account_scopes"></a> [daos\_service\_account\_scopes](#input\_daos\_service\_account\_scopes) | Scopes for the DAOS server service account | `list(string)` | <pre>[<br>  "userinfo-email",<br>  "compute-ro",<br>  "storage-ro"<br>]</pre> | no |
| <a name="input_instance_base_name"></a> [instance\_base\_name](#input\_instance\_base\_name) | MIG instance base names to use | `string` | `null` | no |
| <a name="input_labels"></a> [labels](#input\_labels) | Set of key/value label pairs to assign to daos-server instances | `any` | `{}` | no |
| <a name="input_machine_type"></a> [machine\_type](#input\_machine\_type) | GCP machine type. ie. e2-medium | `string` | `null` | no |
| <a name="input_mig_name"></a> [mig\_name](#input\_mig\_name) | MIG name | `string` | `null` | no |
| <a name="input_network"></a> [network](#input\_network) | GCP network to use | `string` | `null` | no |
| <a name="input_number_of_instances"></a> [number\_of\_instances](#input\_number\_of\_instances) | Number of daos servers to bring up | `number` | `null` | no |
| <a name="input_os_disk_size_gb"></a> [os\_disk\_size\_gb](#input\_os\_disk\_size\_gb) | OS disk size in GB | `number` | `20` | no |
| <a name="input_os_disk_type"></a> [os\_disk\_type](#input\_os\_disk\_type) | OS disk type ie. pd-ssd, pd-standard | `string` | `"pd-ssd"` | no |
| <a name="input_os_family"></a> [os\_family](#input\_os\_family) | OS GCP image family | `string` | `null` | no |
| <a name="input_os_project"></a> [os\_project](#input\_os\_project) | OS GCP image project name | `string` | `null` | no |
| <a name="input_preemptible"></a> [preemptible](#input\_preemptible) | If preemptible instances | `string` | `false` | no |
| <a name="input_project_id"></a> [project\_id](#input\_project\_id) | The GCP project to use | `string` | `null` | no |
| <a name="input_region"></a> [region](#input\_region) | The GCP region to create and test resources in | `string` | `null` | no |
| <a name="input_subnetwork"></a> [subnetwork](#input\_subnetwork) | GCP sub-network to use | `string` | `null` | no |
| <a name="input_subnetwork_project"></a> [subnetwork\_project](#input\_subnetwork\_project) | The GCP project where the subnetwork is defined | `string` | `null` | no |
| <a name="input_template_name"></a> [template\_name](#input\_template\_name) | MIG template name | `string` | `null` | no |
| <a name="input_zone"></a> [zone](#input\_zone) | The GCP zone to create and test resources in | `string` | `null` | no |

## Outputs

No outputs.
<!-- END OF PRE-COMMIT-TERRAFORM DOCS HOOK -->