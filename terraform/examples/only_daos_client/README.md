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

### Update the `access_points` variable

This example assumes there is an existing group of DAOS server instances that the clients will connect to.

The `access_points` variable in the `terraform.tfvars` file should contain a comma delimited list of DAOS server names or IP addresses.

For example, if the existing DAOS server names are

- daos-server-0001
- daos-server-0002
- daos-server-0003

the `access_points` variable should be set to

```
access_points = ["daos-server-0001","daos-server-0002","daos-server-0002"]
```

The `access_points` variable does not need to contain every server in the DAOS cluster.

It only needs enough entries so that if a server is not available there are others to connect to.

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
| <a name="input_client_access_points"></a> [client\_access\_points](#input\_client\_access\_points) | List of servers to add to client .yml files | `list(string)` | `null` | no |
| <a name="input_client_instance_base_name"></a> [client\_instance\_base\_name](#input\_client\_instance\_base\_name) | MIG instance base names to use | `string` | `null` | no |
| <a name="input_client_labels"></a> [client\_labels](#input\_client\_labels) | Set of key/value label pairs to assign to daos-client instances | `any` | `{}` | no |
| <a name="input_client_machine_type"></a> [client\_machine\_type](#input\_client\_machine\_type) | GCP machine type. e.g. e2-medium | `string` | `null` | no |
| <a name="input_client_mig_name"></a> [client\_mig\_name](#input\_client\_mig\_name) | MIG name | `string` | `null` | no |
| <a name="input_client_number_of_instances"></a> [client\_number\_of\_instances](#input\_client\_number\_of\_instances) | Number of daos servers to bring up | `number` | `null` | no |
| <a name="input_client_os_disk_size_gb"></a> [client\_os\_disk\_size\_gb](#input\_client\_os\_disk\_size\_gb) | OS disk size in GB | `number` | `20` | no |
| <a name="input_client_os_disk_type"></a> [client\_os\_disk\_type](#input\_client\_os\_disk\_type) | OS disk type e.g. pd-ssd, pd-standard | `string` | `"pd-ssd"` | no |
| <a name="input_client_os_family"></a> [client\_os\_family](#input\_client\_os\_family) | OS GCP image family | `string` | `null` | no |
| <a name="input_client_os_project"></a> [client\_os\_project](#input\_client\_os\_project) | OS GCP image project name | `string` | `null` | no |
| <a name="input_client_preemptible"></a> [client\_preemptible](#input\_client\_preemptible) | If preemptible client instances | `string` | `true` | no |
| <a name="input_client_template_name"></a> [client\_template\_name](#input\_client\_template\_name) | MIG template name | `string` | `null` | no |
| <a name="input_network"></a> [network](#input\_network) | GCP network to use | `string` | `"default"` | no |
| <a name="input_project_id"></a> [project\_id](#input\_project\_id) | The GCP project to use | `string` | `null` | no |
| <a name="input_region"></a> [region](#input\_region) | The GCP region to create and test resources in | `string` | `null` | no |
| <a name="input_subnetwork"></a> [subnetwork](#input\_subnetwork) | GCP sub-network to use | `string` | `"default"` | no |
| <a name="input_subnetwork_project"></a> [subnetwork\_project](#input\_subnetwork\_project) | The GCP project where the subnetwork is defined | `string` | `null` | no |
| <a name="input_zone"></a> [zone](#input\_zone) | The GCP zone to create and test resources in | `string` | `null` | no |

## Outputs

No outputs.
<!-- END OF PRE-COMMIT-TERRAFORM DOCS HOOK -->
