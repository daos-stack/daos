# Images

This directory contains Cloud Build configuration files, Packer templates and scripts used for building DAOS images with [Cloud Build](https://cloud.google.com/build).

## Pre-Deployment steps required

If you have not done so yet, please complete the steps in [Pre-Deployment Guide](../docs/pre-deployment_guide.md).

## Building DAOS images

The [Pre-Deployment Guide](../docs/pre-deployment_guide.md) includes instructions for building DAOS images.

You can re-build your images with the following commands.

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
