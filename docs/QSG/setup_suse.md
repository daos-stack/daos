# DAOS Set-Up on OpenSUSE



## Introduction


The purpose of this guide is to provide a user with a set of command lines to quickly setup and use DAOS with POSIX on openSUSE/SLES 15.3.

This document covers installation of the DAOS rpms on openSUSE/SLES 15.3 and updating the DAOS configuration files needed by daos servers.

This guide will also describe how to use dfuse in order to take advantage of DAOS support for POSIX. 
For setup instructions on CentOS7 and EL8, refer to the [CentOS7/EL8 setup](setup_centos7_and_el8.md).
For more details reference the DAOS administration guide:
<https://docs.daos.io/v2.0/admin/hardware/>

## Requirements

This Guide requires a minimum of:

- 1 server with PMEM and SSDs connected via infiniband storage network.
- 1 client node.
- 1 admin node without pmem/ssd but on the infiniband storage network.
- All nodes have a base openSUSE or SLES 15.3 installed.

Install pdsh on the admin node
The following steps require two or more hosts which will be divided up
into admin, client, and server roles.  One node can be used for both the
admin and client node.  All nodes must have:

-   sudo access configured

-   password-less ssh configured

-   pdsh installed (or some other means of running multiple remote
    commands in parallel)

In addition the server nodes should also have
[IOMMU enabled](https://docs.daos.io/v2.0/admin/predeployment_check/#enable-iommu).

For the use of the commands outlined on this page the following shell
variables will need to be defined:

-   ADMIN_NODE

-   CLIENT_NODES

-   SERVER_NODES

-   ALL_NODES

For example, if one wanted to use node-1 as their admin node, node-2 and
node-3 as client nodes, and node-\[4-6\] as their server nodes then
these variables would be defined as:

```console
ADMIN_NODE=node-1

CLIENT_NODES=node-2,node-3

SERVER_NODES=node-4,node-5,node-6

ALL_NODES=$ADMIN_NODE,$CLIENT_NODES,$SERVER_NODES
```

!!! note

	If a client node is also serving as an admin node, exclude
	`$ADMIN_NODE` from the `ALL_NODES` assignment to prevent duplication.

	For example:

	`ALL_NODES=$CLIENT_NODES,$SERVER_NODES`

## RPM Installation


In this section the required RPMs will be installed on each of the nodes
based upon their role.  Admin and client nodes require the installation
of the daos-client RPM and the server nodes require the installation of the
daos-server RPM.

1.  Configure access to the DAOS package repository at
    <https://packages.daos.io/v2.0>.


		pdsh -w $ALL_NODES 'sudo zypper ar https://packages.daos.io/v2.0/Leap15/packages/x86_64/ daos_packages'


2. Import GPG key on all nodes:

		pdsh -w $ALL_NODES 'sudo rpm --import https://packages.daos.io/RPM-GPG-KEY'

7.  Perform the additional steps:

		pdsh -w $ALL_NODES 'sudo zypper --non-interactive refresh'



12. Install the DAOS server RPMs on the server nodes:


		pdsh -w $SERVER_NODES 'sudo zypper install -y daos-server'



17. Install the DAOS client RPMs on the client and admin nodes:


		pdsh -w $ALL_NODES -x $SERVER_NODES 'sudo zypper install -y daos-client'



## Hardware Provisioning

In this section, PMem (Intel(R) Optane(TM) persistent memory) and NVME
SSDs will be prepared and configured to be used by DAOS.

!!! note
	PMem preparation is required once per DAOS installation.

!!! note
	For OpenSUSE 15.3 installation, update ipmctl to the latest package available from https://build.opensuse.org/package/binaries/hardware:nvdimm/ipmctl/openSUSE_Leap_15.3

1.     Prepare the pmem devices on Server nodes:

		daos_server storage prepare --scm-only

	Sample Script:

		Preparing locally-attached SCM\...

		Memory allocation goals for SCM will be changed and namespaces
		modified, this will be a destructive operation. Please ensure
		namespaces are unmounted and locally attached SCM & NVMe devices are
		not in use. Please be patient as it may take several minutes and
		subsequent reboot maybe required.

		Are you sure you want to continue? (yes/no)

		yes

		A reboot is required to process new SCM memory allocation goals.

8.  Reboot the server node.

9.  Run the prepare cmdline again:

		daos_server storage prepare --scm-only

	Sample Script:

		Preparing locally-attached SCM\...
		SCM namespaces:
		SCM Namespace	Socket ID	Capacity
		-------------	---------	--------
		pmem0			0 			3.2 TB
		pmem1 			0 			3.2 TB

17. Prepare the NVME devices on Server nodes:

		daos_server storage prepare --nvme-only -u root
		Preparing locally-attached NVMe storage\...

19. Scan the available storage on the Server nodes:

		daos_server storage scan
		Scanning locally-attached storage...

		NVMe PCI		Model				FW Revision	Socket ID	Capacity
		--------		-----				-----------	---------	--------
		0000:5e:00.0	INTEL SSDPE2KE016T8 VDV10170 	0 			1.6 TB
		0000:5f:00.0	INTEL SSDPE2KE016T8 VDV10170 	0 			1.6 TB
		0000:81:00.0	INTEL SSDPED1K750GA E2010475 	1 			750 GB
		0000:da:00.0	INTEL SSDPED1K750GA E2010475 	1 			750 GB

		SCM Namespace	Socket ID	Capacity
		-------------	---------	--------
		pmem0 			0 			3.2 TB
		pmem1 			1 			3.2 TB

## Generate certificates

In this section certificates will be generated and installed for
encrypting the DAOS control plane communications.

Administrative nodes require the following certificate files:

-   CA root certificate (daosCA.crt) owned by the current user

-   Admin certificate (admin.crt) owned by the current user

-   Admin key (admin.key) owned by the current user

Client nodes require the following certificate files:

-   CA root certificate (daosCa.crt) owned by the current user

-   Agent certificate (agent.crt) owned by the daos_agent user

-   Agent key (agent.key) owned by the daos_agent user

Server nodes require the following certificate files:

-   CA root certificate (daosCA.crt) owned by the daos_server user

-   Server certificate (server.crt) owned by the daos_server user

-   Server key (server.key) owned by the daos_server user

-   A copy of the Client certificate (client.crt) owned by the
    daos_server user

See
<https://docs.daos.io/v2.0/admin/deployment/#certificate-configuration>
for more informaation.

!!! note
	The following commands are run from the `$ADMIN_NODE`.

1.  Generate a new set of certificates:

		cd /tmp
		/usr/lib64/daos/certgen/gen_certificates.sh

	!!! note
		These files should be protected from unauthorized access and preserved for future use.

2.  Copy the certificates to a common location on each node in order to
    move them to the final location:

		pdsh -S -w $ALL_NODES -x $(hostname -s) scp -r $(hostname -s):/tmp/daosCA /tmp

3.  Copy the certificates to their default location (/etc/daos) on each
    admin node:

		pdsh -S -w $ADMIN_NODE sudo cp /tmp/daosCA/certs/daosCA.crt /etc/daos/certs/.
		pdsh -S -w $ADMIN_NODE sudo cp /tmp/daosCA/certs/admin.crt /etc/daos/certs/.
		pdsh -S -w $ADMIN_NODE sudo cp /tmp/daosCA/certs/admin.key /etc/daos/certs/.

	!!! note
			If the /etc/daos/certs directory does not exist on the admin nodes then use the following command to create it:

				pdsh -S -w $ADMIN_NODES sudo mkdir /etc/daos/certs

4.  Copy the certificates to their default location (/etc/daos) on each
    client node:

		pdsh -S -w $CLIENT_NODES sudo cp /tmp/daosCA/certs/daosCA.crt /etc/daos/certs/.
		pdsh -S -w $CLIENT_NODES sudo cp /tmp/daosCA/certs/agent.crt /etc/daos/certs/.
		pdsh -S -w $CLIENT_NODES sudo cp /tmp/daosCA/certs/agent.key /etc/daos/certs/.

	!!! note
		If the /etc/daos/certs directory does not exist on the client nodes, use the following command to create it:

			pdsh -S -w $CLIENT_NODES sudo mkdir /etc/daos/certs

5. Copy the certificates to their default location (/etc/daos) on each
    server node:

		pdsh -S -w $SERVER_NODES sudo cp /tmp/daosCA/certs/daosCA.crt /etc/daos/certs/.
		pdsh -S -w $SERVER_NODES sudo cp /tmp/daosCA/certs/server.crt /etc/daos/certs/.
		pdsh -S -w $SERVER_NODES sudo cp /tmp/daosCA/certs/server.key /etc/daos/certs/.
		pdsh -S -w $SERVER_NODES sudo cp /tmp/daosCA/certs/agent.crt /etc/daos/certs/clients/agent.crt

6. Cleanup the temp directory
		pdsh -S -w $ALL_NODES sudo rm -rf /tmp/daosCA

7. Set the ownership of the admin certificates on each admin node:

		pdsh -S -w $ADMIN_NODE sudo chown $USER:$USER /etc/daos/certs/daosCA.crt
		pdsh -S -w $ADMIN_NODE sudo chown $USER:$USER /etc/daos/certs/admin.*

8. Set the ownership of the client certificates on each client node:

		pdsh -S -w $CLIENT_NODES sudo chown $USER:$USER /etc/daos/certs/daosCA.crt
		pdsh -S -w $CLIENT_NODES sudo chown daos_agent:daos_agent /etc/daos/certs/agent.*

9. Set the ownership of the server certificates on each server node:

		pdsh -S -w $SERVER_NODES sudo chown daos_server:daos_server /etc/daos/certs/daosCA.crt
		pdsh -S -w $SERVER_NODES sudo chown daos_server:daos_server /etc/daos/certs/server.*
		pdsh -S -w $SERVER_NODES sudo chown daos_server:daos_server /etc/daos/certs/clients/agent.crt
		pdsh -S -w $SERVER_NODES sudo chown daos_server:daos_server /etc/daos/certs/clients

## Create Configuration Files

In this section the `daos_server`, `daos_agent`, and dmg command
configuration files will be defined. Examples are available at
<https://github.com/daos-stack/daos/tree/release/1.2/utils/config/examples>

1.  Determine the addresses for the NVMe devices on the server
    nodes:

		pdsh -S -w $SERVER_NODES sudo lspci | grep -i nvme

	!!! note
		Save the addresses of the NVMe devices to use with each DAOS server,
		e.g. "81:00.0", from each server node.  This information will be
		used to populate the "bdev_list" server configuration parameter
		below.

2.  Create a server configuration file by modifying the default
    `/etc/daos/daos_server.yml` file on the server nodes.

	An example of the daos_server.yml is presented below.  Copy the modified server yaml file to all the server nodes at `/etc/daos/daos_server.yml.

		name: daos_server

		access_points: ['node-4']
		port: 10001

		transport_config:
			allow_insecure: false
			client_cert_dir: /etc/daos/certs/clients
			ca_cert: /etc/daos/certs/daosCA.crt
			cert: /etc/daos/certs/server.crt
			key: /etc/daos/certs/server.key
		provider: ofi+verbs;ofi_rxm
		nr_hugepages: 4096
		control_log_mask: DEBUG
		control_log_file: /tmp/daos_server.log
		helper_log_file: /tmp/daos_admin.log
		engines:
		-
			targets: 8
			nr_xs_helpers: 0
			fabric_iface: ib0
			fabric_iface_port: 31316
			log_mask: INFO
			log_file: /tmp/daos_engine_0.log
			env_vars:
				- CRT_TIMEOUT=30
			scm_mount: /mnt/daos0
			scm_class: dcpm
			scm_list: [/dev/pmem0]
			bdev_class: nvme
			bdev_list: ["0000:81:00.0"]  # generate regular nvme.conf
		-
			targets: 8
			nr_xs_helpers: 0
			fabric_iface: ib1
			fabric_iface_port: 31416
			log_mask: INFO
			log_file: /tmp/daos_engine_1.log
			env_vars:
				- CRT_TIMEOUT=30
			scm_mount: /mnt/daos1
			scm_class: dcpm
			scm_list: [/dev/pmem1]
			bdev_class: nvme
			bdev_list: ["0000:83:00.0"]  # generate regular nvme.conf

1. Copy the modified server yaml file to all the server nodes at `/etc/daos/daos_server.yml`.

1. Create an agent configuration file by modifying the default `/etc/daos/daos_agent.yml` file on the client nodes.  The following is an example daos_agent.yml. Copy the modified agent yaml file to all the client nodes at `/etc/daos/daos_agent.yml`. 

		name: daos_server
		access_points: ['node-4']

		port: 10001

		transport_config:
			allow_insecure: false
			ca_cert: /etc/daos/certs/daosCA.crt
			cert: /etc/daos/certs/agent.crt
			key: /etc/daos/certs/agent.key
		log_file: /tmp/daos_agent.log

1. Create a dmg configuration file by modifying the default `/etc/daos/daos_control.yml` file on the admin node. The following is an example of the `daos_control.yml`. 

		name: daos_server
		port: 10001
		hostlist: ['node-4', 'node-5', 'node-6']

		transport_config:
			allow_insecure: false
			ca_cert: /etc/daos/certs/daosCA.crt
			cert: /etc/daos/certs/admin.crt
			key: /etc/daos/certs/admin.key

1. Create socket directories

		mkdir /var/run/daos_server
		mkdir /var/run/daos_agent

## Start the DAOS Servers

1. Start daos engines on server nodes:

		pdsh -S -w $SERVER_NODES "sudo systemctl daemon-reload"
		pdsh -S -w $SERVER_NODES "sudo systemctl start daos_server"

1. Check status and format storage:

		# check status
		pdsh -S -w $SERVER_NODES "sudo systemctl status daos_server"

		# if you see following format messages (depending on number of servers), proceed to storage format
		wolf-179: May 05 22:21:03 wolf-179.wolf.hpdd.intel.com daos_server[37431]: Metadata format required on instance 0

		# format storage
		dmg storage format -l $SERVER_NODES --force

1. Verify that all servers have started:

		# system query from ADMIN_NODE
		dmg system query -v

		# all the server ranks should show 'Joined' STATE
		Rank UUID                                 Control Address  Fault Domain                  State  Reason
		---- ----                                 ---------------  ------------                  -----  ------
		0    604c4ffa-563a-49dc-b702-3c87293dbcf3 10.8.1.179:10001 /wolf-179.wolf.hpdd.intel.com Joined
		1    f0791f98-4379-4ace-a083-6ca3ffa65756 10.8.1.179:10001 /wolf-179.wolf.hpdd.intel.com Joined
		2    745d2a5b-46dd-42c5-b90a-d2e46e178b3e 10.8.1.189:10001 /wolf-189.wolf.hpdd.intel.com Joined
		3    ba6a7800-3952-46ce-af92-bba9daa35048 10.8.1.189:10001 /wolf-189.wolf.hpdd.intel.com Joined

## Start the DAOS Agents

1. Start the daos agents on the client nodes:

		# start agents
		pdsh -S -w $CLIENT_NODES "sudo systemctl start daos_agent"


1. (Optional) Check daos_agent status:

		# check status
		pdsh -S -w $CLIENT_NODES "cat /tmp/daos_agent.log"

		# Sample output depending on number of client nodes
		node-2: agent INFO 2021/05/05 22:38:46 DAOS Agent v1.2 (pid 47580) listening on /var/run/daos_agent/daos_agent.sock
		node-3: agent INFO 2021/05/05 22:38:53 DAOS Agent v1.2 (pid 39135) listening on /var/run/daos_agent/daos_agent.sock



