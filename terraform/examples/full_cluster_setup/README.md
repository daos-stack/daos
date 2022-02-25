# Full DAOS cluster setup

Deploys full DAOS cluster with servers and clients.

## Usage

1. Create ```terraform.tfvars``` in this directory or the directory where you're running this example.
2. Copy the ```terraform.tfvars.example``` content into ```terraform.tfvars``` file and update the contents to match your environment.
3. Run below commands to deploy DAOS cluster:

```
terraform init -input=false
terraform plan -out=tfplan -input=false
terraform apply -input=false tfplan
```

To destroy DAOS environment, use below command:

```
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
| <a name="module_daos_server"></a> [daos\_server](#module\_daos\_server) | ../../modules/daos_server | n/a |

## Resources

No resources.

## Inputs

| Name | Description | Type | Default | Required |
|------|-------------|------|---------|:--------:|
| <a name="input_client_instance_base_name"></a> [client\_instance\_base\_name](#input\_client\_instance\_base\_name) | MIG instance base names to use | `string` | `null` | no |
| <a name="input_client_labels"></a> [client\_labels](#input\_client\_labels) | Set of key/value label pairs to assign to daos-client instances | `any` | `{}` | no |
| <a name="input_client_machine_type"></a> [client\_machine\_type](#input\_client\_machine\_type) | GCP machine type. e.g. e2-medium | `string` | `null` | no |
| <a name="input_client_mig_name"></a> [client\_mig\_name](#input\_client\_mig\_name) | MIG name | `string` | `null` | no |
| <a name="input_client_number_of_instances"></a> [client\_number\_of\_instances](#input\_client\_number\_of\_instances) | Number of daos servers to bring up | `number` | `null` | no |
| <a name="input_client_os_disk_size_gb"></a> [client\_os\_disk\_size\_gb](#input\_client\_os\_disk\_size\_gb) | OS disk size in GB | `number` | `20` | no |
| <a name="input_client_os_disk_type"></a> [client\_os\_disk\_type](#input\_client\_os\_disk\_type) | OS disk type e.g. pd-ssd, pd-standard | `string` | `"pd-ssd"` | no |
| <a name="input_client_os_family"></a> [client\_os\_family](#input\_client\_os\_family) | OS GCP image family | `string` | `null` | no |
| <a name="input_client_os_project"></a> [client\_os\_project](#input\_client\_os\_project) | OS GCP image project name | `string` | `null` | no |
| <a name="input_client_template_name"></a> [client\_template\_name](#input\_client\_template\_name) | MIG template name | `string` | `null` | no |
| <a name="input_network"></a> [network](#input\_network) | GCP network to use | `string` | `null` | no |
| <a name="input_preemptible"></a> [preemptible](#input\_preemptible) | If preemptible instances | `string` | `true` | no |
| <a name="input_project_id"></a> [project\_id](#input\_project\_id) | The GCP project to use | `string` | `null` | no |
| <a name="input_region"></a> [region](#input\_region) | The GCP region to create and test resources in | `string` | `null` | no |
| <a name="input_server_daos_disk_count"></a> [server\_daos\_disk\_count](#input\_server\_daos\_disk\_count) | Number of local ssd's to use | `number` | `null` | no |
| <a name="input_server_instance_base_name"></a> [server\_instance\_base\_name](#input\_server\_instance\_base\_name) | MIG instance base names to use | `string` | `null` | no |
| <a name="input_server_labels"></a> [server\_labels](#input\_server\_labels) | Set of key/value label pairs to assign to daos-server instances | `any` | `{}` | no |
| <a name="input_server_machine_type"></a> [server\_machine\_type](#input\_server\_machine\_type) | GCP machine type. e.g. e2-medium | `string` | `null` | no |
| <a name="input_server_mig_name"></a> [server\_mig\_name](#input\_server\_mig\_name) | MIG name | `string` | `null` | no |
| <a name="input_server_number_of_instances"></a> [server\_number\_of\_instances](#input\_server\_number\_of\_instances) | Number of daos servers to bring up | `number` | `null` | no |
| <a name="input_server_os_disk_size_gb"></a> [server\_os\_disk\_size\_gb](#input\_server\_os\_disk\_size\_gb) | OS disk size in GB | `number` | `20` | no |
| <a name="input_server_os_disk_type"></a> [server\_os\_disk\_type](#input\_server\_os\_disk\_type) | OS disk type e.g. pd-ssd, pd-standard | `string` | `"pd-ssd"` | no |
| <a name="input_server_os_family"></a> [server\_os\_family](#input\_server\_os\_family) | OS GCP image family | `string` | `null` | no |
| <a name="input_server_os_project"></a> [server\_os\_project](#input\_server\_os\_project) | OS GCP image project name | `string` | `null` | no |
| <a name="input_server_template_name"></a> [server\_template\_name](#input\_server\_template\_name) | MIG template name | `string` | `null` | no |
| <a name="input_subnetwork"></a> [subnetwork](#input\_subnetwork) | GCP sub-network to use | `string` | `null` | no |
| <a name="input_subnetwork_project"></a> [subnetwork\_project](#input\_subnetwork\_project) | The GCP project where the subnetwork is defined | `string` | `null` | no |
| <a name="input_zone"></a> [zone](#input\_zone) | The GCP zone to create and test resources in | `string` | `null` | no |

## Outputs

No outputs.
<!-- END OF PRE-COMMIT-TERRAFORM DOCS HOOK -->