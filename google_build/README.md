# Google build

This directory contains build definitions for the DAOS server Docker image used in Parallelstore.

## Directory structure

Each of the following contains a `Dockerfile` that produces an image:

* `build_image/`: Build image, containing DAOS build-time dependencies.
* `daos_base_image/`: Base image for the DAOS server image, containing DAOS runtime dependencies.
* `daos_image/`: DAOS server image. Uses the build image and DAOS base image.

## Utilities

Note: If Docker isn't installed on your workstation/cloudtop, see go/installdocker.

* `local_build.sh`: Builds the DAOS server image. Can be run locally on a workstation/cloudtop.