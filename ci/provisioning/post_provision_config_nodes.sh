#!/bin/bash

set -eux

env > /root/last_run-env.txt
if ! grep ":$MY_UID:" /etc/group; then
  groupadd -g "$MY_UID" jenkins
fi
mkdir -p /localhome
if ! grep ":$MY_UID:$MY_UID:" /etc/passwd; then
  useradd -b /localhome -g "$MY_UID" -u "$MY_UID" -s /bin/bash jenkins
fi
mkdir -p /localhome/jenkins/.ssh
cat /tmp/ci_key.pub >> /localhome/jenkins/.ssh/authorized_keys
cat /tmp/ci_key.pub >> /root/.ssh/authorized_keys
mv /tmp/ci_key.pub /localhome/jenkins/.ssh/id_rsa.pub
mv /tmp/ci_key /localhome/jenkins/.ssh/id_rsa
mv /tmp/ci_key_ssh_config /localhome/jenkins/.ssh/config
chmod 700 /localhome/jenkins/.ssh
chmod 600 /localhome/jenkins/.ssh/{authorized_keys,id_rsa*,config}
chown -R jenkins.jenkins /localhome/jenkins/
echo "jenkins ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/jenkins

# defined in ci/functional/post_provision_config_nodes_<distro>.sh
# and catted to the remote node along with this script
post_provision_config_nodes

systemctl enable nfs-server.service
systemctl start nfs-server.service
sync
sync
exit 0
