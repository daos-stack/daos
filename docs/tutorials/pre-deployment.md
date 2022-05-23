# DAOS GCP Pre-Deployment Steps

In this tutorial you will complete the following pre-deployment steps which are required to be done once for your GCP project.

1. Set defaults for Google Cloud CLI (```gcloud```)
2. Enable APIs
3. Create a Cloud NAT
4. Create a Packer image in your GCP project
5. Build DAOS Server and Client images with Packer in Cloud Build

After completing this walkthrough you will be able to run Terraform to deploy DAOS Server and Client instances.

## Project Selection

Select the project that you would like to use for deploying DAOS in GCP.

<walkthrough-project-setup billing="true"></walkthrough-project-billing-setup>

> Note that when running scripts and examples from the [google-cloud-daos](https://github.com/daos-stack/google-cloud-daos) repository, charges will be
> incurred within the selected project.
>
> Always be sure to run ```terraform destroy``` when you no longer need your instances.

Click **Start** to continue

## Set ```gcloud``` defaults

Many of the scripts and examples in the [google-cloud-daos](https://github.com/daos-stack/google-cloud-daos) repository use three default configuration settings in your Google Cloud CLI (```gcloud```) configuration.

The default settings are

1. project
2. region
3. zone

### Set Default Project

To set the default project run

```bash
gcloud config set project <walkthrough-project-id/>
```

### Set Default Region

To set the default region run

```bash
gcloud config set compute/region us-central1
```

The ```us-central1``` region is recommended but feel free to change as needed.

### Set Default Zone

To set the default zone run

```bash
gcloud config set compute/zone us-central1-f
```

The ```us-central1-f``` zone is recommended but feel free to change as needed.

### Defaults Set!

You have now set the necessary defaults required for the scripts and examples in the [google-cloud-daos](https://github.com/daos-stack/google-cloud-daos) repository.

Click **Next** to continue

## Enable APIs

Enable the APIs that are used for building images and deploying DAOS instances

```bash
gcloud services enable cloudbuild.googleapis.com
gcloud services enable compute.googleapis.com
gcloud services enable networkmanagement.googleapis.com
gcloud services enable secretmanager.googleapis.com
gcloud services enable servicemanagement.googleapis.com
gcloud services enable sourcerepo.googleapis.com
gcloud services enable storage-api.googleapis.com
```

Click **Next** to continue

## Create a Cloud NAT

When deploying DAOS server and client instances external IPs are not added to the instances. The instances need to use services that are not accessible on the internal VPC default network as well as the https://packages.daos.io site for installs from DAOS repos.

Therefore, it is necessary to create a [Cloud NAT using Cloud Router](https://cloud.google.com/architecture/building-internet-connectivity-for-private-vms#create_a_nat_configuration_using_cloud_router).

1. Create a Cloud Router instance

    ```bash
    gcloud compute routers create nat-router-us-central1 \
      --network default \
      --region us-central1
    ```

2. Configure the router for Cloud NAT

    ```bash
    gcloud compute routers nats create nat-config \
      --router-region us-central1 \
      --router nat-router-us-central1 \
      --nat-all-subnet-ip-ranges \
      --auto-allocate-nat-external-ips
    ```

Click **Next** to continue

## Create Packer Image

### IAM permissions

The Cloud Build service account requires the editor role.

To grant the editor role to the service account run:

```bash
CLOUD_BUILD_ACCOUNT=$(gcloud projects get-iam-policy <walkthrough-project-id/> --filter="(bindings.role:roles/cloudbuild.builds.builder)" --flatten="bindings[].members" --format="value(bindings.members[])")

gcloud projects add-iam-policy-binding <walkthrough-project-id/> \
  --member "${CLOUD_BUILD_ACCOUNT}" \
  --role roles/compute.instanceAdmin
```

### Build Packer Image

DAOS images are built using [Packer](https://www.packer.io/) in [Cloud Build](https://cloud.google.com/build).

In order to run Packer in Cloud Build you need to provision an instance from an image that has Packer installed.

Therfore, in order to build DAOS images with Packer in Cloud Build, your GCP project must contain a Packer image.

Creating the Packer image only needs to be done once in the GCP project.

Cloud Build provides the [Packer community builder image](https://github.com/GoogleCloudPlatform/cloud-builders-community/tree/master/packer).

To build the Packer image run:

```bash
pushd ~/
git clone https://github.com/GoogleCloudPlatform/cloud-builders-community.git
cd cloud-builders-community/packer
gcloud builds submit .
rm -rf ~/cloud-builders-community
popd
```


Click **Next** to continue

## Build DAOS images

In order to use Terraform to provision DAOS Server and Client instances you need to build images that have DAOS pre-installed.

Build the DAOS Server and Client instances

```bash
pushd images
./build_images.sh --type all
popd
```

It will take a few minutes for the images to build.

Click **Next** to continue

## DAOS GCP Setup Complete

<walkthrough-conclusion-trophy></walkthrough-conclusion-trophy>

You can now begin using Terraform to provision DAOS Server and Client instances in the **<walkthrough-project-id/>** project!

**Next Steps**

- Read the <walkthrough-editor-open-file filePath="terraform/modules/daos_client/README.md">terraform/modules/daos_client/README.md</walkthrough-editor-open-file> file
- Read the <walkthrough-editor-open-file filePath="terraform/modules/daos_server/README.md">terraform/modules/daos_server/README.md</walkthrough-editor-open-file> file
- View the files in the ```terraform/examples/daos_cluster``` directory
- Open a tutorial that walks you through the steps to deploy a DAOS cluster using the ```terraform/examples/daos_cluster``` example.
   ```bash
   teachme ./docs/tutorials/example_daos_cluster.md
   ```
