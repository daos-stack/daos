# DAOS Set-Up on RHEL and Clones

The following instructions detail how to install, set up and start DAOS servers and clients on
two or more nodes.
This document includes instructions for RHEL8-compatible distributions. This includes
RHEL8, Rocky Linux and AlmaLinux.

For setup instructions on OpenSuse, refer to [OpenSuse setup](setup_suse.md).

For more details, including the prerequisite steps before installing DAOS,
reference the [DAOS administration guide](https://docs.daos.io/v2.6/admin/hardware/).

## Requirements

The following steps require two or more hosts which will be divided up
into admin, client, and server roles. One node can be used for multiple
roles. For example the admin role can reside on a server, on a client,
or on a dedicated admin node.

All nodes must have:

- sudo access configured

- password-less ssh configured

- pdsh installed (or some other means of running multiple remote
  commands in parallel)

In addition the server nodes should also have
[IOMMU enabled](https://docs.daos.io/v2.6/admin/predeployment_check/#enable-iommu-optional).

For the use of the commands outlined on this page the following shell
variables will need to be defined:

- ADMIN\_NODES
- CLIENT\_NODES
- SERVER\_NODES
- ALL\_NODES

For example, if you want to use admin-1 as the admin node, client-1 and
client-2 as client nodes, and server-\[1-3\] as server nodes,
these variables would be defined as:

```console
ADMIN_NODES="admin-1"
CLIENT_NODES="client-1,client-2"
SERVER_NODES="server-1,server-2"
ALL_NODES="$ADMIN_NODES,$CLIENT_NODES,$SERVER_NODES"
```

!!! note
    If a client node is also serving as an admin node, exclude
    `$ADMIN_NODES` from the `ALL_NODES` assignment to prevent duplication.
    For example: `ALL_NODES=$CLIENT_NODES,$SERVER_NODES`


## RPM Installation

In this section the required RPMs will be installed on each of the nodes
based upon their role.  Admin and client nodes require the installation
of the daos-client RPM and the server nodes require the installation of the
daos-server RPM.

1. Configure access to the [DAOS package repository](https://packages.daos.io/v2.6/):

		pdsh -w $ALL_NODES 'sudo wget -O /etc/yum.repos.d/daos-packages.repo https://packages.daos.io/v2.6/EL8/packages/x86_64/daos_packages.repo'


2. Import GPG key on all nodes:

		pdsh -w $ALL_NODES 'sudo rpm --import https://packages.daos.io/RPM-GPG-KEY'

3. Install epel-release on all nodes:

		pdsh -w $ALL_NODES 'sudo yum install -y epel-release'

4. Install the `daos-admin` RPMs on the admin nodes:

		pdsh -w $ADMIN_NODES 'sudo yum install -y daos-admin'

5. Install the `daos-server` RPMs on the server nodes:

		pdsh -w $SERVER_NODES 'sudo yum install -y daos-server'

6. Install the `daos-client` RPMs on the client nodes:

		pdsh -w $CLIENT_NODES 'sudo yum install -y daos-client'


## Hardware Provisioning

In this section, PMem (Intel(R) Optane(TM) persistent memory) will be prepared and configured to be
used by DAOS and NVME SSDs will be identified.

1. Prepare the pmem devices on Server nodes:

		daos_server scm prepare

	Sample Script:

		Prepare locally-attached PMem\...

		Memory allocation goals for PMem will be changed and namespaces
		modified, this may be a destructive operation. Please ensure
		namespaces are unmounted and locally attached PMem modules are
		not in use. Please be patient as it may take several minutes and
		subsequent reboot maybe required.

		Are you sure you want to continue? (yes/no)

		yes

		A reboot is required to process new PMem memory allocation goals.

2.  Reboot the server node.

3.  Run the prepare cmdline again:

		daos_server scm prepare

	Sample Script:

		Prepare locally-attached PMem\...
		SCM namespaces:
		SCM Namespace	Socket ID	Capacity
		-------------	---------	--------
		pmem0			0 			3.2 TB
		pmem1 			0 			3.2 TB

4. Scan the available storage on the Server nodes:

		daos_server storage scan
		Scanning locally-attached storage\...

		NVMe PCI		Model				FW Revision	Socket ID	Capacity
		--------		-----				-----------	---------	--------
		0000:81:00.0	INTEL SSDPE2KE016T8 VDV10170 	0 			1.6 TB
		0000:83:00.0	INTEL SSDPE2KE016T8 VDV10170 	1 			1.6 TB

		SCM Namespace	Socket ID	Capacity
		-------------	---------	--------
		pmem0 			0 			3.2 TB
		pmem1 			1 			3.2 TB


## Generate certificates

In this section certificates will be generated and installed for
encrypting the DAOS control plane communications.

Administrative nodes require the following certificate files:

- CA root certificate (daosCA.crt) owned by the current user

- Admin certificate (admin.crt) owned by the current user

- Admin key (admin.key) owned by the current user

Client nodes require the following certificate files:

- CA root certificate (daosCa.crt) owned by the current user

- Agent certificate (agent.crt) owned by the daos\_agent user

- Agent key (agent.key) owned by the daos\_agent user

Server nodes require the following certificate files:

- CA root certificate (daosCA.crt) owned by the daos\_server user

- Server certificate (server.crt) owned by the daos\_server user

- Server key (server.key) owned by the daos\_server user

- A copy of the Client certificate (client.crt) owned by the
  daos\_server user

See [Certificate Configuration](https://docs.daos.io/v2.6/admin/deployment/#certificate-configuration)
for more information.

!!! note
    The following commands are run on **one** of the `$ADMIN_NODES`.

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

		pdsh -S -w $ADMIN_NODES sudo cp /tmp/daosCA/certs/daosCA.crt /etc/daos/certs/.
		pdsh -S -w $ADMIN_NODES sudo cp /tmp/daosCA/certs/admin.crt /etc/daos/certs/.
		pdsh -S -w $ADMIN_NODES sudo cp /tmp/daosCA/certs/admin.key /etc/daos/certs/.

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


		pdsh -S -w $ADMIN_NODES sudo chown $USER:$USER /etc/daos/certs/daosCA.crt
		pdsh -S -w $ADMIN_NODES sudo chown $USER:$USER /etc/daos/certs/admin.\*

8. Set the ownership of the client certificates on each client node:

		pdsh -S -w $CLIENT_NODES sudo chown $USER:$USER /etc/daos/certs/daosCA.crt
		pdsh -S -w $CLIENT_NODES sudo chown daos_agent:daos_agent /etc/daos/certs/agent.\*

9. Set the ownership of the server certificates on each server node:

		pdsh -S -w $SERVER_NODES sudo chown daos_server:daos_server /etc/daos/certs/daosCA.crt
		pdsh -S -w $SERVER_NODES sudo chown daos_server:daos_server /etc/daos/certs/server.\*
		pdsh -S -w $SERVER_NODES sudo chown daos_server:daos_server /etc/daos/certs/clients/agent.crt
		pdsh -S -w $SERVER_NODES sudo chown daos_server:daos_server /etc/daos/certs/clients

## Create Configuration Files

In this section the `daos_server`, `daos_agent`, and dmg command configuration files will be defined.
Examples are available on [github](https://github.com/daos-stack/daos/tree/master/utils/config/examples).

1. Determine the addresses for the NVMe devices on the server nodes:

		pdsh -S -w $SERVER_NODES sudo lspci | grep -i nvme

	!!! note
		Save the addresses of the NVMe devices to use with each DAOS server,
		e.g. \"81:00.0\", from each server node.  This information will be
		used to populate the \"bdev_list\" server configuration parameter
		below.

2. Create a server configuration file by modifying the default
   `/etc/daos/daos_server.yml` file on the server nodes.

	An example of the daos_server.yml is presented below.  Copy the modified server yaml file to all the server nodes at `/etc/daos/daos_server.yml.


		name: daos_server
		access_points:
		- server-1
		port: 10001

		transport_config:
			allow_insecure: false
			client_cert_dir: /etc/daos/certs/clients
			ca_cert: /etc/daos/certs/daosCA.crt
			cert: /etc/daos/certs/server.crt
			key: /etc/daos/certs/server.key
		provider: ofi+verbs;ofi_rxm
		control_log_mask: DEBUG
		control_log_file: /tmp/daos_server.log
		helper_log_file: /tmp/daos_server_helper.log
		engines:
		-
			pinned_numa_node: 0
			targets: 8
			nr_xs_helpers: 2
			fabric_iface: ib0
			fabric_iface_port: 31316
			log_mask: INFO
			log_file: /tmp/daos_engine_0.log
			env_vars:
				- CRT_TIMEOUT=30
			storage:
			-
				class: dcpm
				scm_mount: /mnt/daos0
				scm_list:
				- /dev/pmem0
			-
				class: nvme
				bdev_list:
				- "0000:81:00.0"
		-
			pinned_numa_node: 1
			targets: 8
			nr_xs_helpers: 2
			fabric_iface: ib1
			fabric_iface_port: 31416
			log_mask: INFO
			log_file: /tmp/daos_engine_1.log
			env_vars:
				- CRT_TIMEOUT=30
			storage:
			-
				class: dcpm
				scm_mount: /mnt/daos1
				scm_list:
				- /dev/pmem1
			-
				class: nvme
				bdev_list:
				- "0000:83:00.0"

3. Copy the modified server yaml file to all the server nodes at `/etc/daos/daos_server.yml`.

4. Create an agent configuration file by modifying the default `/etc/daos/daos_agent.yml` file on the client nodes.
   The following is an example `daos_agent.yml`.
   Copy the modified agent yaml file to all the client nodes at `/etc/daos/daos_agent.yml`.

		name: daos_server
		access_points:
		- server-1

		port: 10001

		transport_config:
			allow_insecure: false
			ca_cert: /etc/daos/certs/daosCA.crt
			cert: /etc/daos/certs/agent.crt
			key: /etc/daos/certs/agent.key
		log_file: /tmp/daos_agent.log

5. Create a dmg configuration file by modifying the default `/etc/daos/daos_control.yml` file on the admin node.
   The following is an example of the `daos_control.yml`.

		name: daos_server
		port: 10001
		hostlist:
		- server-1
		- server-2

		transport_config:
			allow_insecure: false
			ca_cert: /etc/daos/certs/daosCA.crt
			cert: /etc/daos/certs/admin.crt
			key: /etc/daos/certs/admin.key


## Start the DAOS Servers

1. Start daos engines on server nodes:

		pdsh -S -w $SERVER_NODES "sudo systemctl daemon-reload"
		pdsh -S -w $SERVER_NODES "sudo systemctl start daos_server"

2. Check status and format storage:

		# check status
		pdsh -S -w $SERVER_NODES "sudo systemctl status daos_server"

		# if you see following format messages (depending on number of servers), proceed to storage format
		server-1: server-1.test.hpdd.intel.com INFO 2023/04/11 23:14:06 SCM format required on instance 1
		server-1: server-1.test.hpdd.intel.com INFO 2023/04/11 23:14:06 SCM format required on instance 0

		# format storage
		dmg storage format -l $SERVER_NODES # can use --force if needed

3. Verify that all servers have started:

		# system query from ADMIN_NODES
		dmg system query -v

		# all the server ranks should show 'Joined' STATE
		Rank UUID                                 Control Address  Fault Domain                  State  Reason
		---- ----                                 ---------------  ------------                  -----  ------
		0    604c4ffa-563a-49dc-b702-3c87293dbcf3 10.8.1.179:10001 /server-1.test.hpdd.intel.com Joined
		1    f0791f98-4379-4ace-a083-6ca3ffa65756 10.8.1.179:10001 /server-1.test.hpdd.intel.com Joined
		2    745d2a5b-46dd-42c5-b90a-d2e46e178b3e 10.8.1.189:10001 /server-2.test.hpdd.intel.com Joined
		3    ba6a7800-3952-46ce-af92-bba9daa35048 10.8.1.189:10001 /server-2.test.hpdd.intel.com Joined


## Start the DAOS Agents

1. Start the daos agents on the client nodes:

		# start agents
		pdsh -S -w $CLIENT_NODES "sudo systemctl start daos_agent"


2. (Optional) Check daos\_agent status:

		# check status
		pdsh -S -w $CLIENT_NODES "cat /tmp/daos_agent.log"

		# Sample output depending on number of client nodes
		client-1: agent INFO 2022/05/05 22:38:46 DAOS Agent v2.6 (pid 47580) listening on /var/run/daos_agent/daos_agent.sock
		client-2: agent INFO 2022/05/05 22:38:53 DAOS Agent v2.6 (pid 39135) listening on /var/run/daos_agent/daos_agent.sock

