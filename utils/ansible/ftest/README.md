# Running DAOS Functional Tests

This document describes how to setup with [Ansible](https://www.ansible.com/) one or more nodes to
run DAOS avocado functional tests.  The ansible playbook also generates two bash scripts allowing
for easy build DAOS binaries, and the ability to run a set of DAOS avocado functional tests.

## Prerequisites

### Sub-Cluster Nodes

One or more nodes of the wolf[^1] cluster powered with a supported linux distribution should be
reserved. At this time, the only supported linux distribution is the Rocky 8.5 which could be
installed for example on the node wolf-666 with the following command:

```bash
nodemgr -n wolf-666 -p daos_ci-rocky8.5 install
```

The node(s) to used should be selected according to the hardware requirements of the test(s) to
launch (e.g. pmem, vmd, etc.).  According to the selected nodes, the yaml configuration file(s) of
the test(s) to launch should eventually be adapted.

> :warning: It is **strongly discouraged** to deploy (i.e. apply the playbook tasks) the DAOS
> functional test platform on nodes used by the end user for other tasks such as day to day
> development.  Indeed, the Ansible playbook will eventually change the configuration of the node
> and thus could break custom settings previously defined by the end user:  rpm package version,
> huge page settings, etc.

[^1]: Nodes of the *boro* cluster should also be supported but have not yet been tested.

### Install of Ansible

Ansible could be easily installed with running the following command from the directory containing
this README.md file:

```bash
python3 -m pip install --user --requirement requirements.txt
```

### Ssh Authorization

Ssh authorized keys shall be configured in a way to allow the user playing the Ansible playbook
to have access, without password, to the root account to all the nodes of the sub-cluster.  The
following command could be used for example to grant root access without password to the wolf-666
node.

```bash
ssh-copy-id root@wolf-666
```
## Deployment of the Functional Test Platform

### Ansible Inventory

The [ansible inventory](https://docs.ansible.com/ansible/latest/user_guide/intro_inventory.html)
list the different nodes of the the sub-cluster and gather them in different groups (which can
freely overlap) according to their roles in the functional test platform.
- **daos\_dev**: This group should contain only one node.  This last one will be used for installing
  DAOS binaries and launch functional tests thanks to generated bash scripts.  Details on how to use
  these scripts will be detailed in following sections.
- **daos\_servers**: This group contains the nodes where DAOS servers will be running.
- **daos\_clients**: This group contains the node where end user application such as *ior* or *fio*
  using DAOS file system will be run.

The inventory should also contain a set of mandatory and optional variables.
- **daos\_runtime\_dir**: mandatory variable defining the shared directory used to install DAOS
  binaries. This directory should be accessible with the same path from all the nodes of the
  sub-cluster.
- **daos\_source\_dir**: mandatory variable only used by the node of the *daos\_dev* group defining
  the path of the directory containing the DAOS source code.
- **daos\_ofi\_interface**: optional variable only used by the node of the daos\_dev group defining
  the network interface to use.  When this variable is not defined, the network interface is
  arbitrarily selected by DAOS.
- **daos\_hugepages\_nb**: optional variable (default value: 4096) only used by the nodes of the
  *daos\_servers* group defining the number of hugepages to be allocated by the linux kernel.
- **daos\_avocado\_version**: optional variable (default value: "2.4.3") only used by the node of
  the *daos\_dev* group defining the version of *avocado* to install.
- **daos\_avocado\_framework\_version**: optional variable (default value: "82.1") only used by the
  node of the *daos\_dev* group defining the version of *avocado\_framework* to install.

Different file format (e.g. YAML, INI, etc.) and file tree structure are supported to define an
ansible inventory.  The following simple ansible inventory describe for example in one YAML file
a simple DAOS functional platform composed of two nodes which are assuming several roles.

```yaml
all:
  vars:
    daos_runtime_dir: /home/foo/daos
  children:
    daos_dev:
      vars:
        daos_source_dir: /home/foo/work/daos
        daos_ofi_interface: eth0
      hosts:
        wolf-666:
    daos_servers:
      vars:
        daos_hugepages_nb: 8182
      hosts:
        wolf-666:
        wolf-999:
    daos_clients:
      hosts:
        wolf-999:
```

As illustrated in the following example the `ansible-inventory` command could be used to check the
content of an inventory.
```bash
ansible-inventory --graph --vars --inventory my-inventory.yml
@all:
  |--@daos_clients:
  |  |--wolf-999
  |  |  |--{daos_hugepages_nb = 8182}
  |  |  |--{daos_runtime_dir = /home/foo/daos}
  |--@daos_dev:
  |  |--wolf-666
  |  |  |--{daos_hugepages_nb = 8182}
  |  |  |--{daos_ofi_interface = eth0}
  |  |  |--{daos_runtime_dir = /home/foo/daos}
  |  |  |--{daos_source_dir = /home/foo/work/daos}
  |  |--{daos_ofi_interface = eth0}
  |  |--{daos_source_dir = /home/foo/work/daos}
  |--@daos_servers:
  |  |--wolf-666
  |  |  |--{daos_hugepages_nb = 8182}
  |  |  |--{daos_ofi_interface = eth0}
  |  |  |--{daos_runtime_dir = /home/foo/daos}
  |  |  |--{daos_source_dir = /home/foo/work/daos}
  |  |--wolf-999
  |  |  |--{daos_hugepages_nb = 8182}
  |  |  |--{daos_runtime_dir = /home/foo/daos}
  |  |--{daos_hugepages_nb = 8182}
  |--@ungrouped:
  |--{daos_runtime_dir = /home/foo/daos}
```

### Playing Playbook

When the inventory is defined, then the
[playbook](https://docs.ansible.com/ansible/latest/user_guide/playbooks_intro.html)
*ftest.yml* could be played thanks to the `ansible-playbook` command.

```bash
ansible-playbook -i my-inventory.yml ftest.yml
```

If the play succeeds, the different nodes of the platform should be well configured and the two bash
scripts `daos-launch.sh` and `daos-make.sh` should be available in the DAOS runtime directory
defined in the inventory. Usage of these two scripts will be detailed in the following sections.

> :bulb: As *ftest.yml* playbook is
> [idempotent](https://docs.ansible.com/ansible/latest/reference_appendices/glossary.html#term-Idempotency),
> it could be safely run several times without side effect.

## Installing DAOS Binaries

Building and installing binaries could be done thanks to the generated bash script `daos-make.sh`.
This last script supports several options and the two sub-commands `install` and `update`.

More details on the supported options could be found with running the command with the `--help`
option.

The `install` sub-command should be used to install/reinstall from scratch all the DAOS binaries and
their dependencies such as the *spdk* or the *mercury* libraries. For example, the *foo* user could
use the following command line to install or reinstall the DAOS binaries and its dependencides into
the `/home/foo/daos/install` directory.

```bash
/home/foo/daos/daos-make.sh -v -j 32 -f install
```

When the previous *install* step has been done and the DAOS source code tests have been updated, the
DAOS binaries could be build and reinstalled thanks to the `update` sub-command.  This last one
should be far quicker than a full reinstall.

```bash
/home/foo/daos/daos-make.sh -v -j 32 update
```

> :bulb: The option `-j 32` (i.e. one per core) seems to be a good compromise between compilation
> reliability and speed.

## Launching Functional Tests

Launching the DAOS functional tests could be done thanks to the generated bash script
`daos-launch.sh`. This last script supports several options and it is also possible to give some
options to the original `launch.py` python script of the DAOS functional test platform.

More details on the supported options and the way to passing options to `launch.py` could be found
with running the command with the `--help` option.

After successfully installing the DAOS binaries and dependencies, the user *foo* could run all the
test(s) with the tag `hello_world` thanks to the following command line.


```bash
/home/foo/daos/daos-launch.sh -v -- --nvme=auto  hello_world
```
