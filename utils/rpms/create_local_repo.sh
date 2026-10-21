#!/bin/bash
# Creates a local DAOS repo from rpms in current directory

createrepo -d .
cat << EOF | sudo tee /etc/yum.repos.d/daos_local.repo
[DAOS]
name=DAOS local repo
baseurl=file://$(pwd)
enabled=1
gpgcheck=0
EOF
sudo dnf clean all
