#!/bin/bash

# Configure below variables to your needs
#--------------------------------------------------------
ID="" # Identifier for deploying multiple environments in GCP
PREEMPTIBLE_INSTANCES="true"
NUMBER_OF_SERVERS_INSTANCES="1"
DAOS_DISK_COUNT=8
NUMBER_OF_CLIENTS_INSTANCES="1"
SERVER_MACHINE_TYPE=n2-highmem-32 # n2-custom-20-131072 n2-custom-40-262144 n2-highmem-32 n2-standard-2
CLIENT_MACHINE_TYPE=c2-standard-16 # c2-standard-16 n2-standard-2
CRT_TIMEOUT=300
SCM_SIZE=100
STONEWALL_TIME=3
POOL_SIZE="$(( 375 * ${DAOS_DISK_COUNT} * ${NUMBER_OF_SERVERS_INSTANCES} / 1000 ))TB"
CONTAINER_REPLICATION_FACTOR="rf:0"
SSH_USER="daos-user"

# Terraform environmental variables
export TF_VAR_project_id=""
export TF_VAR_network="default"
export TF_VAR_subnetwork="default"
export TF_VAR_subnetwork_project="${TF_VAR_project_id}"
export TF_VAR_region="us-central1"
export TF_VAR_zone="us-central1-f"
export TF_VAR_preemptible="${PREEMPTIBLE_INSTANCES}"
# Servers
export TF_VAR_server_number_of_instances=${NUMBER_OF_SERVERS_INSTANCES}
export TF_VAR_server_daos_disk_count=${DAOS_DISK_COUNT}
export TF_VAR_server_instance_base_name="daos-server-${ID}"
export TF_VAR_server_os_disk_size_gb=20
export TF_VAR_server_os_disk_type="pd-ssd"
export TF_VAR_server_template_name="daos-server-${ID}"
export TF_VAR_server_mig_name="daos-server-${ID}"
export TF_VAR_server_machine_type="${SERVER_MACHINE_TYPE}"
export TF_VAR_server_os_project="${TF_VAR_project_id}"
export TF_VAR_server_os_family="daos-server"
# Clients
export TF_VAR_client_number_of_instances=${NUMBER_OF_CLIENTS_INSTANCES}
export TF_VAR_client_instance_base_name="daos-client-${ID}"
export TF_VAR_client_os_disk_size_gb=20
export TF_VAR_client_os_disk_type="pd-ssd"
export TF_VAR_client_template_name="daos-client-${ID}"
export TF_VAR_client_mig_name="daos-client-${ID}"
export TF_VAR_client_machine_type="${CLIENT_MACHINE_TYPE}"
export TF_VAR_client_os_project="${TF_VAR_project_id}"
export TF_VAR_client_os_family="daos-client"

#######################
#  Create hosts file  #
#######################

CLIENT_NAME="daos-client-${ID}"
SERVER_NAME="daos-server-${ID}"

rm -f hosts
unset CLIENTS
unset SERVERS
unset ALL_NODES

for ((i=1; i <= ${NUMBER_OF_CLIENTS_INSTANCES} ; i++))
do
    CLIENTS+="${CLIENT_NAME}-$(printf %04d ${i}) "
    echo ${CLIENT_NAME}-$(printf %04d ${i})>>hosts
done

for ((i=1; i <= ${NUMBER_OF_SERVERS_INSTANCES} ; i++))
do
    SERVERS+="${SERVER_NAME}-$(printf %04d ${i}) "
done

ALL_NODES="${SERVERS} ${CLIENTS}"
export ALL_NODES

export SERVERS
export CLIENTS

DAOS_FIRST_SERVER=$(echo ${SERVERS} | awk '{print $1}')
DAOS_FIRST_CLIENT=$(echo ${CLIENTS} | awk '{print $1}')

SERVERS_LIST_WITH_COMMA=$(echo ${SERVERS} | tr ' ' ',')
