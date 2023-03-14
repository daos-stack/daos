# My personal WIP notes

######################
# Install ftest deps #
######################
clush -B -w $ALL_NODES "sudo dnf install -y python3-avocado{,-plugins-{output-html,varianter-yaml-to-mux}} clustershell"


###################################################
# Examples for getting and renaming release repos #
###################################################
ALL_NODES=boro-[24-27]
clush -B -w $ALL_NODES "sudo rpm --import https://packages.daos.io/RPM-GPG-KEY-2023"
clush -B -w $ALL_NODES "sudo rpm --import https://packages.daos.io/RPM-GPG-KEY"
clush -B -w $ALL_NODES 'sudo wget -O /etc/yum.repos.d/daos-packages-v2.2.repo https://packages.daos.io/v2.2/EL8/packages/x86_64/daos_packages.repo'
clush -B -w $ALL_NODES "sudo sed -i 's/\[daos-packages\]/\[daos-packages-2.2.0\]/g' /etc/yum.repos.d/daos-packages-v2.2.repo"
clush -B -w $ALL_NODES 'sudo wget -O /etc/yum.repos.d/daos-packages-v2.4.1.repo https://packages.daos.io/private/v2.4.1/EL8/packages/x86_64/daos_packages.repo'
clush -B -w $ALL_NODES "sudo sed -i 's/\[daos-packages\]/\[daos-packages-2.4.1\]/g' /etc/yum.repos.d/daos-packages-v2.4.1.repo"

# I actually keep the repo files in my home directory and copy to all nodes like this
# See the referenced dir below for examples
ALL_NODES=boro-[24-27]
clush -B -w $ALL_NODES "sudo cp /home/dbohning/repos/* /etc/yum.repos.d/"


########################################
# Revert to 2.2.0 before testing again #
########################################
# These are my steps but untested in other envs
ALL_NODES=boro-[24-27]
VERSION="2.2.0-4.el8"
clush -S -B -w $ALL_NODES "sudo systemctl stop daos_agent; sudo systemctl stop daos_server; \
                        sudo dnf remove -y daos && \
                        sudo dnf config-manager --enable daos-stack-deps-el-8-x86_64-stable-local-artifactory && \
                        sudo dnf install -y ior go hdf5-vol-daos hdf5-vol-daos-mpich && \
                        sudo rpm -e --nodeps daos capstone daos-client libisa-l libisa-l_crypto mercury && \
                        sudo dnf config-manager --disable daos-stack-deps-el-8-x86_64-stable-local-artifactory && \
                        sudo dnf config-manager --disable daos-packages-2.4.1 && \
                        sudo dnf install -y daos-server-tests-${VERSION} daos-tests-${VERSION} && \
                        sudo dnf config-manager --enable daos-packages-2.4.1 &&
                        rpm -qa | grep daos | sort;" || echo 'Failed';


# Rebuild just ftest locally
# Or however you rebuild ftest python changes
~/bin/daos_rebuild_ftest

# To get local avocado to work with RPM install
# Need to replace path to local install with "/usr"
# TODO: need a proper configurable way to do this
sed -i 's/"PREFIX".*/  "PREFIX": "\/usr"/g' .build_vars.json

# Run launch.py
# Really just however you would run the test "test_upgrade_downgrade"
# NOTE: DON'T FORGET THE BUILD_VARS
DAOS_CLIENTS=boro-[24]
DAOS_SERVERS=boro-[25-27]
export PYTHONPATH=/home/dbohning/daos/install/lib/daos/TESTING/ftest:$PYTHONPATH; \
cd ~/daos/install/lib/daos/TESTING/ftest/;  \
sed -i 's/"PREFIX".*/  "PREFIX": "\/usr"/g' .build_vars.json  \
./launch.py --provider "ofi+tcp;ofi_rxm" -aro -tc $DAOS_CLIENTS -ts $DAOS_SERVERS test_upgrade_downgrade;









##############
# MISC EXTRA #
##############

# Copy configs from ftest to nodes
DAOS_CLIENTS=XXXX
DAOS_SERVERS=XXXX
clush -w $DAOS_CLIENTS 'sudo cp /home/dbohning/avocado/job-results/latest/daos_configs.boro-25/daos_agent.yml /etc/daos/daos_agent.yml'; \
clush -w $DAOS_CLIENTS 'sudo cp /home/dbohning/avocado/job-results/latest/daos_configs.boro-25/daos_control.yml /etc/daos/daos_control.yml'; \
clush -w $DAOS_CLIENTS 'sudo cp /home/dbohning/avocado/job-results/latest/daos_configs.boro-25/daos_server.yml /etc/daos/daos_server.yml'; \
clush -w $DAOS_SERVERS 'sudo cp /home/dbohning/avocado/job-results/latest/daos_configs.boro-25/daos_agent.yml /etc/daos/daos_agent.yml'; \
clush -w $DAOS_SERVERS 'sudo cp /home/dbohning/avocado/job-results/latest/daos_configs.boro-25/daos_control.yml /etc/daos/daos_control.yml'; \
clush -w $DAOS_SERVERS 'sudo cp /home/dbohning/avocado/job-results/latest/daos_configs.boro-25/daos_server.yml /etc/daos/daos_server.yml';

# Compare rpms between two nodes
NODE1=XXXX
NODE2=XXXX
diff <(clush -B -w $NODE1 'rpm -qa | sort' 2>&1) <(clush -B -w $NODE2 'rpm -qa | sort' 2>&1)
