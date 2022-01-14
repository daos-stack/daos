#!/bin/bash

# Copyright 2019 Google LLC.
# This software is provided as-is,
# without warranty or representation for any use or purpose.
# Your use of it is subject to your agreements with Google.

#
# To build both DAOS client and server images:
#   ./make_images.sh
#
# To build DAOS client images only:
#   ./make_images.sh client
#
# To build DAOS server images only:
#   ./make_images.sh server
#

set -e
trap 'echo "Unexpected and unchecked error. Exiting."' ERR

# Set environment variable defaults if not already set
: "${IMAGE_TYPE:=all}"

if [[ ! -z $1 ]]; then
  IMAGE_TYPE=$(echo $1 | tr '[A-Z]' '[a-z]')
  if [[ ! $IMAGE_TYPE =~ ^(all|server|client)$ ]]; then
    echo "Invalid value passed for first arg."
    echo "Valid values are 'all', 'client', 'server'"
    exit 1
  fi
fi

PROJECT=$(gcloud info --format="value(config.project)")
FWRULENAME=gcp-cloudbuild-ssh


# The service account used here should have been already created
#by the "packer_build" step.  We are just checking here.
CLOUD_BUILD_ACCOUNT=$(gcloud projects get-iam-policy "${PROJECT}" \
--filter="(bindings.role:roles/cloudbuild.builds.builder)"  \
--flatten="bindings[].members" \
--format="value(bindings.members[])" \
--limit=1)
echo "Packer will be using service account ${CLOUD_BUILD_ACCOUNT}"


# Add cloudbuild SA permissions
gcloud projects add-iam-policy-binding "${PROJECT}" \
  --member "${CLOUD_BUILD_ACCOUNT}" \
  --role roles/compute.instanceAdmin.v1

gcloud projects add-iam-policy-binding "${PROJECT}" \
  --member "${CLOUD_BUILD_ACCOUNT}" \
  --role roles/iam.serviceAccountUser


# Check if we have an ssh firewall rule for cloudbuild in place already
FWLIST=$(gcloud compute --project="${PROJECT}" \
  firewall-rules list \
  --filter name="${FWRULENAME}" \
  --sort-by priority \
  --format='value(name)')

if [[ -z $FWLIST ]]; then
  # Setup firewall rule to allow ssh from clould build.
  # FIXME: Needs to be fixed to restric to IP range
  # for clound build only once we know what that is.
  echo "Setting up firewall rule for ssh and clouldbuild"
  gcloud compute --project="${PROJECT}" firewall-rules create "${FWRULENAME}" \
  --direction=INGRESS --priority=1000 --network=default --action=ALLOW \
  --rules=tcp:22 --source-ranges=0.0.0.0/0
else
  echo "Firewall rule for ssh and cloud build already in place. "
fi


# Build images.
# Increase timeout to 1hr to make sure we don't time out
if [[ $IMAGE_TYPE =~ ^(all|server)$ ]]; then
    printf "\nBuilding server image(s)\n\n"
    gcloud builds submit --timeout=1800s \
     --substitutions=_PROJECT_ID="${PROJECT}" \
     --config=packer_cloudbuild-server.yaml .
fi

if [[ $IMAGE_TYPE =~ ^(all|client)$ ]]; then
    printf "\nBuilding client image(s)\n\n"
    gcloud builds submit --timeout=1800s \
     --substitutions=_PROJECT_ID="${PROJECT}" \
     --config=packer_cloudbuild-client.yaml .
fi

# Remove ssh firewall
gcloud -q compute --project="${PROJECT}" firewall-rules delete "${FWRULENAME}"
