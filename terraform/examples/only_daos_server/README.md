# DAOS Server Example

This example Terraform configuration demonstrates how to use the [DAOS Server Terraform Module](../../modules/daos_server) in your own Terraform code to deploy a group of DAOS servers.

## Setup

The following steps must be performed prior to deploying DAOS servers.

1. Set defaults for Google Cloud CLI (```gcloud```)
2. Create a Packer image in your GCP project
3. Build DAOS server images

If you have not completed these steps yet, click the button below to open an interactive walkthrough in [Cloud Shell](https://cloud.google.com/shell). After completing the walkthrough your GCP project will contain the images required to run this Terraform example.

[![DAOS on GCP Setup](http://gstatic.com/cloudssh/images/open-btn.png)](https://console.cloud.google.com/cloudshell/open?git_repo=https://github.com/daos-stack/google-cloud-daos&cloudshell_git_branch=main&shellonly=true&tutorial=docs/tutorials/daosgcp_setup.md)

## Terraform Files

List of Terraform files in this example

| Filename                      | Description                                                                     |
| ----------------------------- | ------------------------------------------------------------------------------- |
| main.tf                       | Main Terrform configuration file containing resource definitions                |
| variables.tf                  | Variable definitions for variables used in main.tf                              |
| versions.tf                   | Provider definitions                                                            |
| terraform.tfvars.perf.example | Pre-Configured set of set of variables focused on performance                   |
| terraform.tfvars.tco.example  | Pre-Configured set of set of variables focused on lower total cost of ownership |

## Create a terraform.tfvars file

Before you run `terraform apply` with this example you need to create a `terraform.tfvars` file in the `terraform/examples/only_daos_server` directory.

The `terraform.tfvars` file will contain the variable values that are used by the `main.tf` configuration file.

To ensure a successful deployment of a DAOS cluster there are two `terraform.tfvars.*.example` files that you can choose from.

You will need to decide which of these files you will copy to `terraform.tfvars` and then modify to contain your GCP project info.


### The terraform.tfvars.tco.example file

The `terraform.tfvars.tco.example` configuration will result in a smaller cluster deployment that is more affordable to operate due to fewer instances.

This is a good choice if you just want to deploy a DAOS cluster to learn how to use DAOS.

To use the `terraform.tfvars.tco.example` file run

```bash
cp terraform.tfvars.tco.example terraform.tfvars
```

### The terraform.tfvars.perf.example file

The `terraform.tfvars.perf.example` configuration will result in a larger cluster deployment that costs more to operate but will have better performance due to more instances being deployed.

To use the ```terraform.tfvars.perf.example``` file run

```bash
cp terraform.tfvars.tco.example terraform.tfvars
```

### Update `terraform.tfvars` with your project id

Now that you have a `terraform.tfvars` file you need to replace the `<project_id>` placeholder in the file with your project id.

To update the project id in `terraform.tfvars` run

```bash
PROJECT_ID=$(gcloud config list --format 'value(core.project)')
sed -i "s/<project_id>/${PROJECT_ID}/g" terraform.tfvars
```

## Deploy DAOS Server Instances

> **Billing Notification!**
>
> Running this example will incur charges in your project.
>
> To avoid surprises, be sure to monitor your costs associated with running this example.
>
> Don't forget to shut down the DAOS servers with `terraform destroy` when you are finished.

To deploy the DAOS server instances

```bash
cd terraform/examples/only_daos_server
terraform init -input=false
terraform plan -out=tfplan -input=false
terraform apply -input=false tfplan
```

## Remove DAOS server deployment

To destroy the DAOS server instances run

```bash
terraform destroy
```

# Terraform Documentation for this Example

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
| <a name="module_daos_server"></a> [daos\_server](#module\_daos\_server) | ../../modules/daos_server | n/a |

## Resources

No resources.

## Inputs

| Name | Description | Type | Default | Required |
|------|-------------|------|---------|:--------:|
| <a name="input_network_name"></a> [network\_name](#input\_network\_name) | Name of the GCP network to use | `string` | `"default"` | no |
| <a name="input_project_id"></a> [project\_id](#input\_project\_id) | The GCP project to use | `string` | n/a | yes |
| <a name="input_region"></a> [region](#input\_region) | The GCP region to create and test resources in | `string` | n/a | yes |
| <a name="input_server_daos_crt_timeout"></a> [server\_daos\_crt\_timeout](#input\_server\_daos\_crt\_timeout) | crt\_timeout | `number` | `300` | no |
| <a name="input_server_daos_disk_count"></a> [server\_daos\_disk\_count](#input\_server\_daos\_disk\_count) | Number of local ssd's to use | `number` | `16` | no |
| <a name="input_server_daos_disk_type"></a> [server\_daos\_disk\_type](#input\_server\_daos\_disk\_type) | Daos disk type to use. For now only suported one is local-ssd | `string` | `"local-ssd"` | no |
| <a name="input_server_daos_scm_size"></a> [server\_daos\_scm\_size](#input\_server\_daos\_scm\_size) | scm\_size | `number` | `200` | no |
| <a name="input_server_instance_base_name"></a> [server\_instance\_base\_name](#input\_server\_instance\_base\_name) | MIG instance base names to use | `string` | `"daos-server"` | no |
| <a name="input_server_labels"></a> [server\_labels](#input\_server\_labels) | Set of key/value label pairs to assign to daos-server instances | `any` | `{}` | no |
| <a name="input_server_machine_type"></a> [server\_machine\_type](#input\_server\_machine\_type) | GCP machine type. ie. e2-medium | `string` | `"n2-custom-36-215040"` | no |
| <a name="input_server_mig_name"></a> [server\_mig\_name](#input\_server\_mig\_name) | MIG name | `string` | `"daos-server"` | no |
| <a name="input_server_number_of_instances"></a> [server\_number\_of\_instances](#input\_server\_number\_of\_instances) | Number of daos servers to bring up | `number` | `4` | no |
| <a name="input_server_os_disk_size_gb"></a> [server\_os\_disk\_size\_gb](#input\_server\_os\_disk\_size\_gb) | OS disk size in GB | `number` | `20` | no |
| <a name="input_server_os_disk_type"></a> [server\_os\_disk\_type](#input\_server\_os\_disk\_type) | OS disk type ie. pd-ssd, pd-standard | `string` | `"pd-ssd"` | no |
| <a name="input_server_os_family"></a> [server\_os\_family](#input\_server\_os\_family) | OS GCP image family | `string` | `"daos-server-centos-7"` | no |
| <a name="input_server_os_project"></a> [server\_os\_project](#input\_server\_os\_project) | OS GCP image project name. Defaults to project\_id if null. | `string` | `null` | no |
| <a name="input_server_pools"></a> [server\_pools](#input\_server\_pools) | If provided, this module will generate a script to create a list of pools. pool attributes have to be specified in a format acceptable by [dmg](https://docs.daos.io/v2.0/admin/pool_operations/) and daos. | <pre>list(object({<br>    pool_name  = string<br>    pool_size  = string<br>    containers = list(string)<br>    })<br>  )</pre> | `[]` | no |
| <a name="input_server_preemptible"></a> [server\_preemptible](#input\_server\_preemptible) | If preemptible instances | `string` | `false` | no |
| <a name="input_server_service_account"></a> [server\_service\_account](#input\_server\_service\_account) | Service account to attach to the instance. See https://www.terraform.io/docs/providers/google/r/compute_instance_template.html#service_account. | <pre>object({<br>    email  = string,<br>    scopes = set(string)<br>  })</pre> | <pre>{<br>  "email": null,<br>  "scopes": [<br>    "https://www.googleapis.com/auth/devstorage.read_only",<br>    "https://www.googleapis.com/auth/logging.write",<br>    "https://www.googleapis.com/auth/monitoring.write",<br>    "https://www.googleapis.com/auth/servicecontrol",<br>    "https://www.googleapis.com/auth/service.management.readonly",<br>    "https://www.googleapis.com/auth/trace.append"<br>  ]<br>}</pre> | no |
| <a name="input_server_template_name"></a> [server\_template\_name](#input\_server\_template\_name) | MIG template name | `string` | `"daos-server"` | no |
| <a name="input_subnetwork_name"></a> [subnetwork\_name](#input\_subnetwork\_name) | Name of the GCP sub-network to use | `string` | `"default"` | no |
| <a name="input_subnetwork_project"></a> [subnetwork\_project](#input\_subnetwork\_project) | The GCP project where the subnetwork is defined | `string` | `null` | no |
| <a name="input_zone"></a> [zone](#input\_zone) | The GCP zone to create and test resources in | `string` | n/a | yes |

## Outputs

No outputs.
<!-- END OF PRE-COMMIT-TERRAFORM DOCS HOOK -->
