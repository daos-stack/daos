# DAOS Images

This file describes how to build a DAOS client and server images

## Dependencies
We leverage cloud build and use a packer container image that we assume exists in your project.
If you have not build the Packer container image please do so now before continuing.

Please visit the tutorial [quickstart-packer](https://cloud.google.com/cloud-build/docs/quickstart-packer).
You can leverage code from [cloud-builders-community](https://github.com/GoogleCloudPlatform/cloud-builders-community.git)
is the first step (the [packer folder](https://github.com/GoogleCloudPlatform/cloud-builders-community/tree/master/packer)).
You must have a packer docker container in your project in order for the rest of the scripts in this repository to work.

### Set a default project

To set your default project, run:
``` gcloud config set project <your project id>```

## Making images

Make both DAOS server and client images

```bash
cd images/
./make_images.sh
```

Make DAOS server image

```bash
cd images/
./make_images.sh server
```

Make DAOS client image

```bash
cd images/
./make_images.sh client
```
