#!/bin/bash

set -e
trap 'echo "Hit an unexpected and unchecked error. Exiting."' ERR

# Load needed variables
source ./configure.sh

if [ ! -f images_were_built.flag ]
then
    echo "##########################"
    echo "#  Building DAOS images  #"
    echo "##########################"
    pushd ../../../images
    ./make_images.sh
    popd
    touch images_were_built.flag
fi

echo "######################################"
echo "#  Deploying DAOS Servers & Clients  #"
echo "######################################"
pushd ../full_cluster_setup
terraform init -input=false
terraform plan -out=tfplan -input=false
terraform apply -input=false tfplan
popd

echo "#  Wait for instances"
sleep 10

echo "# Add external IP to first client, so that it will be accessible over normal SSH"
gcloud compute instances add-access-config ${DAOS_FIRST_CLIENT} --zone ${TF_VAR_zone} && sleep 10
IP=$(gcloud compute instances describe ${DAOS_FIRST_CLIENT} | grep natIP | awk '{print $2}')

echo "##########################"
echo "#  Configure SSH access  #"
echo "##########################"
echo "# Prepare SSH key"
rm -f ./id_rsa* ; ssh-keygen -t rsa -b 4096 -C "root" -N '' -f id_rsa
echo "${SSH_USER}:$(cat id_rsa.pub)" > keys.txt

for node in $ALL_NODES
do
    echo "#  Configuring SSH on ${node}"
    # Disable OSLogin to be able to connect with SSH keys uploaded in next command
    gcloud compute instances add-metadata ${node} --metadata enable-oslogin=FALSE
    # Upload SSH key to instance, so that you could login to instance over SSH
    gcloud compute instances add-metadata ${node} --metadata-from-file ssh-keys=keys.txt
done

echo "# Copy SSH key to first DAOS client"
scp -i id_rsa -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    id_rsa \
    id_rsa.pub \
    "${SSH_USER}@${IP}:~/.ssh"
ssh -i id_rsa -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    ${SSH_USER}@${IP} \
    "printf 'Host *\n  StrictHostKeyChecking no\n  IdentityFile ~/.ssh/id_rsa\n' > ~/.ssh/config && \
    chmod -R 600 .ssh/*"

echo "# Copy files"
scp -i id_rsa -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    clean.sh \
    configure.sh \
    setup_io500.sh \
    "${SSH_USER}@${IP}:~"

echo "#########################################################################"
echo "#  Now run setup_io500.sh script on ${DAOS_FIRST_CLIENT}"
echo "#  SSH to it using this command:"
echo "#  ssh -i id_rsa ${SSH_USER}@${IP}"
echo "#########################################################################"
