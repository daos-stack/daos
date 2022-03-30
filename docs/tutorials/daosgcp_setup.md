# DAOS GCP Setup

In this walkthrough you will

1. Set defaults for Google Cloud CLI (```gcloud```)
2. Create a Packer image in your GCP project
3. Build DAOS Server and Client images with Packer in Cloud Build

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
gcloud config set project {{project-id}}
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

## Create Packer Image

DAOS images are built using [Packer](https://www.packer.io/) in [Cloud Build](https://cloud.google.com/build).

In order to run Packer in Cloud Build you need to provision an instance from an image that has Packer installed.

Therfore, in order to build DAOS images with Packer in Cloud Build, your GCP project must contain a Packer image.

Creating the Packer image only needs to be done once in the GCP project.

### Enable APIs

To enable the necessary APIs for Cloud Build run:

```bash
gcloud services enable sourcerepo.googleapis.com
gcloud services enable compute.googleapis.com
gcloud services enable servicemanagement.googleapis.com
gcloud services enable storage-api.googleapis.com
```

### Required IAM permissions

The Cloud Build service account requires the editor role.

To grant the editor role to the service account run:

```bash
CLOUD_BUILD_ACCOUNT=$(gcloud projects get-iam-policy {{project-id}} --filter="(bindings.role:roles/cloudbuild.builds.builder)" --flatten="bindings[].members" --format="value(bindings.members[])")

gcloud projects add-iam-policy-binding {{project-id}} \
  --member "${CLOUD_BUILD_ACCOUNT}" \
  --role roles/compute.instanceAdmin
```

### Create the Packer Image

Cloud Build provides the [Packer community builder image](https://github.com/GoogleCloudPlatform/cloud-builders-community/tree/master/packer).

To build the Packer image run:

```bash
pushd .
cd ~/
git clone https://github.com/GoogleCloudPlatform/cloud-builders-community.git
cd cloud-builders-community/packer
gcloud builds submit .
rm -rf ~/cloud-builders-community
popd
```

<br>

You have completed the necessary steps to create your Packer image which will be used to build DAOS images with Packer in Cloud Build.

Click **Next** to continue

## Build DAOS Server and Client images

In order to use Terraform to provision DAOS Server and Client instances you need to build images that have DAOS pre-installed.

To build the DAOS Server and Client instances run:

```bash
pushd .
cd images
./build_images.sh --type all
popd
```

It will take a few minutes for the images to build.

Wait for the image build to complete.

Click **Next** to continue

## DAOS GCP Setup Complete

<walkthrough-conclusion-trophy></walkthrough-conclusion-trophy>

You can now begin using Terraform to provision DAOS Server and Client instances in the **{{project-id}}** project!

**Next Steps**

- Read the <walkthrough-editor-open-file filePath="terraform/modules/daos_client/README.md">terraform/modules/daos_client/README.md</walkthrough-editor-open-file> file
- Read the <walkthrough-editor-open-file filePath="terraform/modules/daos_server/README.md">terraform/modules/daos_server/README.md</walkthrough-editor-open-file> file
- View the files in the ```terraform/examples/daos_cluster``` directory
- Open a tutorial that walks you through the process of deploying a DAOS cluster using the ```terraform/examples/daos_cluster``` example.
   ```bash
   cloudshell launch-tutorial ./docs/tutorials/example_daos_cluster.md
   ```
