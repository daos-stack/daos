#!/bin/bash

set -e
trap 'echo "Hit an unexpected and unchecked error. Exiting."' ERR

# Load needed variables
source ./configure.sh

# Set SSH options for ssh and scp commands
SSH_OPTS="-i id_rsa -o IdentitiesOnly=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"

log() {
  local msg="|  $1  |"
  line=$(printf "${msg}" | sed 's/./-/g')
  tput setaf 14 # set Cyan color
  printf -- "\n${line}\n${msg}\n${line}\n"
  tput sgr0 # reset color
}

# Build the DAOS disk images if they don't exist in the project
if ! gcloud compute images list | grep -q "${TF_VAR_server_os_family}"; then
    log "Building DAOS server image: ${TF_VAR_server_os_family}"
    pushd ../../../images
    ./make_images.sh "server"
    popd
fi

if ! gcloud compute images list | grep -q "${TF_VAR_client_os_family}"; then
    log "Building DAOS client image: ${TF_VAR_client_os_family}"
    pushd ../../../images
    ./make_images.sh "client"
    popd
fi

log "Deploying DAOS Servers and Clients"
pushd ../full_cluster_setup
terraform init -input=false
terraform plan -out=tfplan -input=false
terraform apply -input=false tfplan
popd

printf "\nWait for DAOS client instances\n\n"
gcloud compute instance-groups managed wait-until ${TF_VAR_client_template_name} --stable --zone ${TF_VAR_zone}
echo "#  Wait for DAOS server instances"
gcloud compute instance-groups managed wait-until ${TF_VAR_server_template_name} --stable --zone ${TF_VAR_zone}

printf "\nAdd external IP to first client\n\n"
gcloud compute instances add-access-config ${DAOS_FIRST_CLIENT} --zone ${TF_VAR_zone} && sleep 10
FIRST_CLIENT_IP=$(gcloud compute instances describe ${DAOS_FIRST_CLIENT} | grep natIP | awk '{print $2}')

log "Configure SSH access"
printf "\nCreate SSH key\n\n"
rm -f ./id_rsa* ; ssh-keygen -t rsa -b 4096 -C "${SSH_USER}" -N '' -f id_rsa
echo "${SSH_USER}:$(cat id_rsa.pub)" > keys.txt

printf "\nConfiguring SSH for user '${SSH_USER}' on all nodes\n\n"
for node in $ALL_NODES
do
    # Disable OSLogin to be able to connect with SSH keys uploaded in next command
    gcloud compute instances add-metadata ${node} --metadata enable-oslogin=FALSE && \
    # Upload SSH key to instance, so that you could login to instance over SSH
    gcloud compute instances add-metadata ${node} --metadata-from-file ssh-keys=keys.txt &
done

# Wait for SSH configuring tasks to finish
wait

printf "\nCopy SSH key to first DAOS client\n\n"
scp ${SSH_OPTS} \
    id_rsa \
    id_rsa.pub \
    "${SSH_USER}@${FIRST_CLIENT_IP}:~/.ssh"
ssh ${SSH_OPTS} ${SSH_USER}@${FIRST_CLIENT_IP} \
    "printf 'Host *\n  StrictHostKeyChecking no\n  IdentityFile ~/.ssh/id_rsa\n' > ~/.ssh/config && \
    chmod -R 600 .ssh/*"

log "Copy files to first client"
scp ${SSH_OPTS} \
    clean.sh \
    configure.sh \
    run_io500-sc21.sh \
    install_io500-sc21.sh \
    install_mpifileutils.sh \
    "${SSH_USER}@${FIRST_CLIENT_IP}:~"

ssh ${SSH_OPTS} ${SSH_USER}@${FIRST_CLIENT_IP} "chmod +x ~/*.sh && chmod -x ~/configure.sh"

log "DAOS servers and clients deployed successfully"
gcloud compute instances list --filter="name:daos*"

printf "

To run an IO500 benchmark:

1. Log into the first client
   ssh -i id_rsa ${SSH_USER}@${FIRST_CLIENT_IP}

2. Run IO500
   ~/run_io500-sc21.sh

"
