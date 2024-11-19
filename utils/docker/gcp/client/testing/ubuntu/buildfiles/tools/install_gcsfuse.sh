#!/bin/bash
set -e
trap 'echo "An unexpected error occurred. Exiting."' ERR

GCSFUSE_REPO=gcsfuse-"$(lsb_release -c -s)"
export GCSFUSE_REPO

echo "deb [signed-by=/usr/share/keyrings/cloud.google.asc] https://packages.cloud.google.com/apt $GCSFUSE_REPO main" | sudo tee /etc/apt/sources.list.d/gcsfuse.list

curl https://packages.cloud.google.com/apt/doc/apt-key.gpg | sudo tee /usr/share/keyrings/cloud.google.asc

sudo apt-get update

sudo apt-get install -y gcsfuse
