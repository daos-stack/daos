# DAOS Images

The following instructions describe how to build DAOS client and server images.

## Dependencies

Before you can build DAOS images you will need to do the following steps once.

1. Install the Google Cloud Platform SDK and set a default project, region and zone

   See https://cloud.google.com/sdk/docs/install

   After you have installed the Google Cloud Platform SDK you need to set some
   defaults.

   To set defaults and initialize the configuration run:

   ```shell
   gcloud config set project <your project id>
   gcloud config set compute/region <region>
   gcloud config set compute/zone <zone>

   # Initialize the configuration
   gcloud init
   ```

2. Create a Packer Disk Image

   The DAOS images are built in Google Cloud build using a Packer container
   image that exists in your project.

   Before you can build DAOS images you need to build the Packer container image.

   Please visit the tutorial [quickstart-packer](https://cloud.google.com/cloud-build/docs/quickstart-packer).

   You can leverage code from [cloud-builders-community](https://github.com/GoogleCloudPlatform/cloud-builders-community.git)
   in the first step (the [packer folder](https://github.com/GoogleCloudPlatform/cloud-builders-community/tree/master/packer)).


## Building DAOS images

Build both DAOS server and client images

```bash
cd images/
./build_images.sh --type all
```

Build a DAOS server image

```bash
cd images/
./build_images.sh --type server
```

Build a DAOS client image

```bash
cd images/
./build_images.sh --type client
```
