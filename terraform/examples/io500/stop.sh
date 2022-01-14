#!/bin/bash

set -e
trap 'echo "Hit an unexpected and unchecked error. Exiting."' ERR

# Load needed variables
source ./configure.sh

printf "
-------------------------------------------------------------------------------
Destroying DAOS Servers & Clients
-------------------------------------------------------------------------------
"

pushd ../full_cluster_setup
terraform destroy -auto-approve
popd
