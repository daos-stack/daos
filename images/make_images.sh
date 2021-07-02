#!/bin/bash

# Copyright 2019 Google LLC.
# This software is provided as-is,
# without warranty or representation for any use or purpose.
# Your use of it is subject to your agreements with Google.

PROJECT=$(gcloud info --format="value(config.project)")
fwrulename=gcp-cloudbuild-ssh


# The service account used here should have been already created
#by the "packer_build" step.  We are just checking here.
CLOUD_BUILD_ACCOUNT=$(gcloud projects get-iam-policy $PROJECT \
--filter="(bindings.role:roles/cloudbuild.builds.builder)"  \
--flatten="bindings[].members" --format="value(bindings.members[])")
echo "Packer will be using service account ${CLOUD_BUILD_ACCOUNT}"

# Add cloudbuild SA permissions
gcloud projects add-iam-policy-binding $PROJECT \
  --member $CLOUD_BUILD_ACCOUNT \
  --role roles/compute.instanceAdmin.v1

gcloud projects add-iam-policy-binding $PROJECT \
  --member $CLOUD_BUILD_ACCOUNT \
  --role roles/iam.serviceAccountUser


# check if we have an ssh firewall rule for cloudbuild in place already
fwlist=$(gcloud compute --project=${PROJECT} firewall-rules list --filter name=${fwrulename} \
  --sort-by priority \
  --format='value(name)')

if [ -z $fwlist ] ;
  then
    #setup firewall rule to allow ssh from clould build.
    #FIXME: Needs to be fixed to restric to IP range
    #for clound build only once we know what that is.
    echo "setting up firewall rule for ssh and clouldbuild."
    gcloud compute --project=${PROJECT} firewall-rules create ${fwrulename} \
    --direction=INGRESS --priority=1000 --network=default --action=ALLOW \
    --rules=tcp:22 --source-ranges=0.0.0.0/0
  else
    echo "Firewall rule for ssh and cloud build already in place. "
fi


#build image. We need to make sure we don't time out so we increase to 1hr.
gcloud builds submit --timeout=1800s \
  --substitutions=_PROJECT_ID=${PROJECT} \
  --config=packer_cloudbuild.yaml .


gcloud builds submit --timeout=1800s \
  --substitutions=_PROJECT_ID=${PROJECT} \
  --config=packer_cloudbuild-client.yaml .

# remove ssh firewall
gcloud -q compute --project=${PROJECT} firewall-rules delete ${fwrulename}

