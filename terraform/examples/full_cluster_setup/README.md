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
