# Interoperability Testing
## Description
These tests are intended to be ran from a source install of ftest, but against a DAOS RPM installation. The ftest source should generally be the latest code from master, which should work with any supported DAOS RPMs.

## Setup ftest environment
It's recommended to use a virtual environment for pip dependencies, and update your PYTHONPATH to use your source install of ftest.  
For example, to create a new python virtual environment with ftest dependencies:
```
python3 -m venv ~/venv_interop
source ~/venv_interop/bin/activate
python3 -m pip install --upgrade pip
python3 -m pip install -r /path/to/daos-stack/daos/requirements-ftest.txt

export PYTHONPATH=/path/to/daos/install/lib/daos/TESTING/ftest:$PYTHONPATH;
```
Fake a `pydaos` install by linking to the `pydaos` RPM install.  
TODO: `pydaos` should be properly installed with the version of python being used.  
For example:
```
ln -s /usr/lib64/python3.6/site-packages/pydaos ~/venv_interop/lib/python3.6/site-packages/pydaos
```

## Set Reference to Clients and Servers
Set these if you are following the instructions below:
```
DAOS_CLIENTS=X
DAOS_SERVERS=Y
ALL_NODES=$DAOS_CLIENTS,$DAOS_SERVERS
```

## Examples for setting up unique repos
These are examples only and specific system paths will depend on how your nodes are configured.  
The basic idea is to configure a unique repo corresponding to each version of DAOS to be tested. This allows the test framework to list the RPMs available in each repo/version.
### Setup 2.6.1 repo on EL 8.8
```
clush -S -B -w "$ALL_NODES" "sudo wget -O /etc/yum.repos.d/daos-packages-v2.6.1.repo https://packages.daos.io/v2.6.1/EL8/packages/x86_64/daos_packages.repo && \
                             sudo sed -i 's/\[daos-packages\]/\[daos-packages-v2.6.1\]/g' /etc/yum.repos.d/daos-packages-v2.6.1.repo"
# Test RPMs for v2.6.1 are not publicly available, so get them from the Jenkins artifacts.
cat << EOT >> /tmp/repo.txt
[daos-packages-v2.6.1-test]
name=DAOS v2.6.1 Packages Packages Test
baseurl=https://build.hpdd.intel.com/job/daos-stack/job/daos/job/release%2F2.6/195/artifact/artifacts/el8/
enabled=1
gpgcheck=0
protect=1
EOT
clush -S -B -w "$ALL_NODES" --copy /tmp/repo.txt && \
clush -S -B -w "$ALL_NODES" "sudo mv /tmp/repo.txt /etc/yum.repos.d/daos-packages-v2.6.1-test.repo"
```
## Setup 2.6.2 repo on EL 8.8
```
clush -S -B -w "$ALL_NODES" "sudo wget -O /etc/yum.repos.d/daos-packages-v2.6.2.repo https://packages.daos.io/v2.6.2/EL8/packages/x86_64/daos_packages.repo && \
                             sudo sed -i 's/\[daos-packages\]/\[daos-packages-v2.6.2\]/g' /etc/yum.repos.d/daos-packages-v2.6.2.repo"
```

## Install "old" version initially on all nodes
This requires looking at the repo to figure out the exact versions of DAOS and dependencies for the release.  
TODO: automate this in the test framework setup.  
For example, to list daos and mercury:
```
dnf list --repo daos-packages-v2.6.2 | grep -E '^daos|^mercury'
```
```
clush -S -B -w $DAOS_CLIENTS "sudo dnf install -y daos-2.6.2-2.el8 \
                                                  mercury-2.4.0~rc5-4.el8 \
                                                  daos-admin-2.6.2-2.el8  \
                                                  daos-client-2.6.2-2.el8 \
                                                  daos-client-tests-2.6.2-2.el8 \
                                                  ior mpich hdf5-vol-daos-mpich" && \
clush -S -B -w $DAOS_SERVERS "sudo dnf install -y daos-2.6.2-2.el8 \
                                                  mercury-2.4.0~rc5-4.el8 \
                                                  daos-admin-2.6.2-2.el8 \
                                                  daos-server-2.6.2-2.el8" && \
clush -S -B -w "$ALL_NODES" "rpm -qa | grep -E '^daos|^mercury' | sort"
```

## Make sure mpich is loaded
TODO: handle this in the test framework setup.  
For example:
```
module load mpi/mpich-x86_64
```

## Configure test parameters
The test to be ran is `test_upgrade_downgrade`.  
TODO: add details and examples.

## Run launch.py
Double check that you are using your source install of ftest as mentioned above!  
Execution is similar to other functional tests. For example:
```
cd /path/to/daos/install/lib/daos/TESTING/ftest/;  \
python3 ./launch.py --provider "ofi+tcp;ofi_rxm" -aro -tc "$DAOS_CLIENTS" -ts "$DAOS_SERVERS" test_upgrade_downgrade;
```
