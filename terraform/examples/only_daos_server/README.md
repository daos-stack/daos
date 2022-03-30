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
| <a name="input_network"></a> [network](#input\_network) | GCP network to use | `string` | `"default"` | no |
| <a name="input_project_id"></a> [project\_id](#input\_project\_id) | The GCP project to use | `string` | `null` | no |
| <a name="input_region"></a> [region](#input\_region) | The GCP region to create and test resources in | `string` | `null` | no |
| <a name="input_server_daos_crt_timeout"></a> [server\_daos\_crt\_timeout](#input\_server\_daos\_crt\_timeout) | crt\_timeout | `number` | `null` | no |
| <a name="input_server_daos_disk_count"></a> [server\_daos\_disk\_count](#input\_server\_daos\_disk\_count) | Number of local ssd's to use | `number` | `null` | no |
| <a name="input_server_daos_scm_size"></a> [server\_daos\_scm\_size](#input\_server\_daos\_scm\_size) | scm\_size | `number` | `null` | no |
| <a name="input_server_instance_base_name"></a> [server\_instance\_base\_name](#input\_server\_instance\_base\_name) | MIG instance base names to use | `string` | `null` | no |
| <a name="input_server_labels"></a> [server\_labels](#input\_server\_labels) | Set of key/value label pairs to assign to daos-server instances | `any` | `{}` | no |
| <a name="input_server_machine_type"></a> [server\_machine\_type](#input\_server\_machine\_type) | GCP machine type. e.g. e2-medium | `string` | `null` | no |
| <a name="input_server_mig_name"></a> [server\_mig\_name](#input\_server\_mig\_name) | MIG name | `string` | `null` | no |
| <a name="input_server_number_of_instances"></a> [server\_number\_of\_instances](#input\_server\_number\_of\_instances) | Number of daos servers to bring up | `number` | `null` | no |
| <a name="input_server_os_disk_size_gb"></a> [server\_os\_disk\_size\_gb](#input\_server\_os\_disk\_size\_gb) | OS disk size in GB | `number` | `20` | no |
| <a name="input_server_os_disk_type"></a> [server\_os\_disk\_type](#input\_server\_os\_disk\_type) | OS disk type e.g. pd-ssd, pd-standard | `string` | `"pd-ssd"` | no |
| <a name="input_server_os_family"></a> [server\_os\_family](#input\_server\_os\_family) | OS GCP image family | `string` | `null` | no |
| <a name="input_server_os_project"></a> [server\_os\_project](#input\_server\_os\_project) | OS GCP image project name | `string` | `null` | no |
| <a name="input_server_preemptible"></a> [server\_preemptible](#input\_server\_preemptible) | If preemptible server instances | `string` | `true` | no |
| <a name="input_server_template_name"></a> [server\_template\_name](#input\_server\_template\_name) | MIG template name | `string` | `null` | no |
| <a name="input_subnetwork"></a> [subnetwork](#input\_subnetwork) | GCP sub-network to use | `string` | `"default"` | no |
| <a name="input_subnetwork_project"></a> [subnetwork\_project](#input\_subnetwork\_project) | The GCP project where the subnetwork is defined | `string` | `null` | no |
| <a name="input_zone"></a> [zone](#input\_zone) | The GCP zone to create and test resources in | `string` | `null` | no |

## Outputs

No outputs.
<!-- END OF PRE-COMMIT-TERRAFORM DOCS HOOK -->
