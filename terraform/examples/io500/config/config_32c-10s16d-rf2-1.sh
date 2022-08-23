#!/bin/bash
# Copyright 2022 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# ------------------------------------------------------------------------------
# Configuration: 32 clients, 10 servers, 16 disks per server
# IO500 Config:  io500-isc22.config-template.daos-rf2.ini
# ------------------------------------------------------------------------------

# Optional identifier to allow multiple DAOS clusters in the same GCP
# project by using this ID in the DAOS server and client instance names.
# Typically, this would contain the username of each user who is running
# the terraform/examples/io500/start.sh script in one GCP project.
# This should be set to a constant value and not the value of an
# environment variable such as '${USER}' which changes depending on where this
# file gets sourced.
ID=""

# Server and client instances
PREEMPTIBLE_INSTANCES="false"
SSH_USER="daos-user"
DAOS_ALLOW_INSECURE="false"

# Server(s)
# Total disks per server: (16 disks * 375GB)/1000 = 6TB
# Total space for 8 servers:  8 * 6TB = 48TB
# Total Amount of SCM needed for tier-ratio to 3: 48TB * 3% = 1.44GB
# SCM needed per server = 1.44GB / 8 = 180GB
# vCPUs needed per server: 1 CPU per disk + 2 for the server
#                          (16 + 2) * 8 = 144 vCPUs per server



DAOS_SERVER_INSTANCE_COUNT="10"
DAOS_SERVER_MACHINE_TYPE=n2-highmem-32
DAOS_SERVER_DISK_COUNT=16
DAOS_SERVER_CRT_TIMEOUT=300
DAOS_SERVER_SCM_SIZE=200
DAOS_SERVER_GVNIC=false

# Client(s)
DAOS_CLIENT_INSTANCE_COUNT="32"
DAOS_CLIENT_MACHINE_TYPE=c2-standard-16
DAOS_CLIENT_GVNIC=false

# Storage
DAOS_POOL_SIZE="$(awk -v disk_count=${DAOS_SERVER_DISK_COUNT} -v server_count=${DAOS_SERVER_INSTANCE_COUNT} 'BEGIN {pool_size = 350 * disk_count * server_count / 1000; print pool_size"TB"}')"
DAOS_CONT_REPLICATION_FACTOR="rf:0"

# IO500
IO500_TEST_CONFIG_ID="GCP-32C-10S16d-rf2-1"
IO500_STONEWALL_TIME=30  # Number of seconds to run the benchmark
IO500_INI="io500-isc22.config-template.daos-rf2.ini"

# ------------------------------------------------------------------------------
# Modify instance base names if ID variable is set
# ------------------------------------------------------------------------------
DAOS_CONFIG_NAME="${DAOS_CLIENT_INSTANCE_COUNT}c-${DAOS_SERVER_INSTANCE_COUNT}s-${DAOS_SERVER_DISK_COUNT}d"
DAOS_SERVER_BASE_NAME="${DAOS_SERVER_BASE_NAME:-daos-server-${DAOS_CONFIG_NAME}}"
DAOS_CLIENT_BASE_NAME="${DAOS_CLIENT_BASE_NAME:-daos-client-${DAOS_CONFIG_NAME}}"
if [[ -n ${ID} ]]; then
    DAOS_SERVER_BASE_NAME="${DAOS_SERVER_BASE_NAME}-${ID}"
    DAOS_CLIENT_BASE_NAME="${DAOS_CLIENT_BASE_NAME}-${ID}"
fi

# ------------------------------------------------------------------------------
# Terraform environment variables
# It's rare that these will need to be changed.
# ------------------------------------------------------------------------------
export TF_VAR_project_id="$(gcloud info --format="value(config.project)")"
export TF_VAR_network="default"
export TF_VAR_subnetwork="default"
export TF_VAR_subnetwork_project="${TF_VAR_project_id}"
export TF_VAR_region="us-central1"
export TF_VAR_zone="us-central1-f"
export TF_VAR_allow_insecure="${DAOS_ALLOW_INSECURE}"

# Servers
export TF_VAR_server_preemptible=${PREEMPTIBLE_INSTANCES}
export TF_VAR_server_number_of_instances=${DAOS_SERVER_INSTANCE_COUNT}
export TF_VAR_server_daos_disk_count=${DAOS_SERVER_DISK_COUNT}
export TF_VAR_server_daos_crt_timeout=${DAOS_SERVER_CRT_TIMEOUT}
export TF_VAR_server_daos_scm_size=${DAOS_SERVER_SCM_SIZE}
export TF_VAR_server_instance_base_name="${DAOS_SERVER_BASE_NAME}"
export TF_VAR_server_os_disk_size_gb=20
export TF_VAR_server_os_disk_type="pd-ssd"
export TF_VAR_server_template_name="${DAOS_SERVER_BASE_NAME}"
export TF_VAR_server_mig_name="${DAOS_SERVER_BASE_NAME}"
export TF_VAR_server_machine_type="${DAOS_SERVER_MACHINE_TYPE}"
export TF_VAR_server_os_project="${TF_VAR_project_id}"
export TF_VAR_server_os_family="daos-server-io500-centos-7"
export TF_VAR_server_gvnic="${DAOS_SERVER_GVNIC}"

# Clients
export TF_VAR_client_preemptible=${PREEMPTIBLE_INSTANCES}
export TF_VAR_client_number_of_instances=${DAOS_CLIENT_INSTANCE_COUNT}
export TF_VAR_client_instance_base_name="${DAOS_CLIENT_BASE_NAME}"
export TF_VAR_client_os_disk_size_gb=20
export TF_VAR_client_os_disk_type="pd-ssd"
export TF_VAR_client_template_name="${DAOS_CLIENT_BASE_NAME}"
export TF_VAR_client_mig_name="${DAOS_CLIENT_BASE_NAME}"
export TF_VAR_client_machine_type="${DAOS_CLIENT_MACHINE_TYPE}"
export TF_VAR_client_os_project="${TF_VAR_project_id}"
export TF_VAR_client_os_family="daos-client-io500-hpc-centos-7"
export TF_VAR_client_gvnic="${DAOS_CLIENT_GVNIC}"
