# DAOS Client Example

This example Terraform configuration demonstrates how to use the [DAOS Client Terraform Module](../../modules/daos_client) in your own Terraform code to deploy a group of DAOS clients.

## Setup

The following steps must be performed prior to deploying DAOS clients.

1. Set defaults for Google Cloud CLI (```gcloud```)
2. Create a Packer image in your GCP project
3. Build DAOS client images

If you have not completed these steps yet, click the button below to open an interactive walkthrough in [Cloud Shell](https://cloud.google.com/shell). After completing the walkthrough your GCP project will contain the images required to run this Terraform example.

[![DAOS on GCP Setup](http://gstatic.com/cloudssh/images/open-btn.png)](https://console.cloud.google.com/cloudshell/open?git_repo=https://github.com/daos-stack/google-cloud-daos&cloudshell_git_branch=main&shellonly=true&tutorial=docs/tutorials/daosgcp_setup.md)

## Terraform Files

List of Terraform files in this example

| Filename                      | Description                                                                     |
| ----------------------------- | ------------------------------------------------------------------------------- |
| main.tf                       | Main Terrform configuration file containing resource definitions                |
| variables.tf                  | Variable definitions for variables used in main.tf                              |
| versions.tf                   | Provider definitions                                                            |
| terraform.tfvars.example | Pre-Configured set of set of variables                   |

## Create a terraform.tfvars file

Before you run `terraform apply` with this example you need to create a `terraform.tfvars` file in the `terraform/examples/only_daos_server` directory.

The `terraform.tfvars` file will contain the variable values that are used by the `main.tf` configuration file.

Copy the `terraform.tfvars.example` to `terraform.tfvars` and then modify it to contain your GCP project info.


```bash
cp terraform.tfvars.example terraform.tfvars
GCP_PROJECT=$(gcloud config list --format='value(core.project)')
sed -i "s/<project_id>/${GCP_PROJECT}/g" terraform.tfvars
```

### Update the client_*_yml variables

Typically, when using both the `terraform/modules/daos_server` and `terraform/modules/daos_client` modules in the same Terraform configuration the `client_daos_agent_yml` and `client_daos_control_yml` variables would be set using output variables from the `terraform/modules/daos_server` module.

In this client only example we are assuming that the DAOS server instances already exist and are not deployed in your Terraform configuration.  You are only deploying clients in your Terrform configuration.

Therefore, you will not have the output variables from the `terraform/modules/daos_server` module to pass to the `client_daos_agent_yml` and `client_daos_control_yml` variables.

In this case [heredocs](https://www.terraform.io/language/expressions/strings#indented-heredocs) are used to set the values `client_*_yml` variables.

The `client_daos_agent_yml` variable should contain the contents of the `/etc/daos/daos_agent.yml` file on the DAOS client instances.

The `client_daos_control_yml` variable should contain the contents of the `/etc/daos/daos_control.yml` file on the DAOS client instances.

See the values of the variables in the `terraform.tfvars.example` file.

The names of the servers will need to be modified to match the names of the DAOS server instances your clients will communicate with.

## Deploy DAOS Client Instances

> **Billing Notification!**
>
> Running this example will incur charges in your project.
>
> To avoid surprises, be sure to monitor your costs associated with running this example.
>
> Don't forget to shut down the DAOS clients with `terraform destroy` when you are finished.

To deploy the DAOS client instances

```bash
cd terraform/examples/only_daos_client
terraform init -input=false
terraform plan -out=tfplan -input=false
terraform apply -input=false tfplan
```

## Remove DAOS client deployment

To destroy the DAOS client instances run

```bash
terraform destroy
```

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

## Resources

No resources.

## Inputs

| Name | Description | Type | Default | Required |
|------|-------------|------|---------|:--------:|
| <a name="input_client_daos_agent_yml"></a> [client\_daos\_agent\_yml](#input\_client\_daos\_agent\_yml) | YAML to configure the daos agent. | `string` | n/a | yes |
| <a name="input_client_daos_control_yml"></a> [client\_daos\_control\_yml](#input\_client\_daos\_control\_yml) | YAML configuring DAOS control. | `string` | n/a | yes |
| <a name="input_client_instance_base_name"></a> [client\_instance\_base\_name](#input\_client\_instance\_base\_name) | MIG instance base names to use | `string` | `"daos-client"` | no |
| <a name="input_client_labels"></a> [client\_labels](#input\_client\_labels) | Set of key/value label pairs to assign to daos-client instances | `any` | `{}` | no |
| <a name="input_client_machine_type"></a> [client\_machine\_type](#input\_client\_machine\_type) | GCP machine type. ie. c2-standard-16 | `string` | `"c2-standard-16"` | no |
| <a name="input_client_mig_name"></a> [client\_mig\_name](#input\_client\_mig\_name) | MIG name | `string` | `"daos-client"` | no |
| <a name="input_client_number_of_instances"></a> [client\_number\_of\_instances](#input\_client\_number\_of\_instances) | Number of daos clients to bring up | `number` | `4` | no |
| <a name="input_client_os_disk_size_gb"></a> [client\_os\_disk\_size\_gb](#input\_client\_os\_disk\_size\_gb) | OS disk size in GB | `number` | `20` | no |
| <a name="input_client_os_disk_type"></a> [client\_os\_disk\_type](#input\_client\_os\_disk\_type) | OS disk type ie. pd-ssd, pd-standard | `string` | `"pd-ssd"` | no |
| <a name="input_client_os_family"></a> [client\_os\_family](#input\_client\_os\_family) | OS GCP image family | `string` | `"daos-client-hpc-centos-7"` | no |
| <a name="input_client_os_project"></a> [client\_os\_project](#input\_client\_os\_project) | OS GCP image project name. Defaults to project\_id if null. | `string` | `null` | no |
| <a name="input_client_preemptible"></a> [client\_preemptible](#input\_client\_preemptible) | If preemptible instances | `string` | `false` | no |
| <a name="input_client_service_account"></a> [client\_service\_account](#input\_client\_service\_account) | Service account to attach to the instance. See https://www.terraform.io/docs/providers/google/r/compute_instance_template.html#service_account. | <pre>object({<br>    email  = string,<br>    scopes = set(string)<br>  })</pre> | <pre>{<br>  "email": null,<br>  "scopes": [<br>    "https://www.googleapis.com/auth/devstorage.read_only",<br>    "https://www.googleapis.com/auth/logging.write",<br>    "https://www.googleapis.com/auth/monitoring.write",<br>    "https://www.googleapis.com/auth/servicecontrol",<br>    "https://www.googleapis.com/auth/service.management.readonly",<br>    "https://www.googleapis.com/auth/trace.append"<br>  ]<br>}</pre> | no |
| <a name="input_client_template_name"></a> [client\_template\_name](#input\_client\_template\_name) | MIG template name | `string` | `"daos-client"` | no |
| <a name="input_network_name"></a> [network\_name](#input\_network\_name) | Name of the GCP network to use | `string` | `"default"` | no |
| <a name="input_project_id"></a> [project\_id](#input\_project\_id) | The GCP project to use | `string` | n/a | yes |
| <a name="input_region"></a> [region](#input\_region) | The GCP region to create and test resources in | `string` | n/a | yes |
| <a name="input_subnetwork_name"></a> [subnetwork\_name](#input\_subnetwork\_name) | Name of the GCP sub-network to use | `string` | `"default"` | no |
| <a name="input_subnetwork_project"></a> [subnetwork\_project](#input\_subnetwork\_project) | The GCP project where the subnetwork is defined | `string` | `null` | no |
| <a name="input_zone"></a> [zone](#input\_zone) | The GCP zone to create and test resources in | `string` | n/a | yes |

## Outputs

No outputs.
<!-- END OF PRE-COMMIT-TERRAFORM DOCS HOOK -->
