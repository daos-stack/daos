..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2010-2014 Intel Corporation.

VMDQ and DCB Forwarding Sample Application
==========================================

The VMDQ and DCB Forwarding sample application is a simple example of packet processing using the DPDK.
The application performs L2 forwarding using VMDQ and DCB to divide the incoming traffic into queues.
The traffic splitting is performed in hardware by the VMDQ and DCB features of the Intel® 82599 and X710/XL710 Ethernet Controllers.

Overview
--------

This sample application can be used as a starting point for developing a new application that is based on the DPDK and
uses VMDQ and DCB for traffic partitioning.

The VMDQ and DCB filters work on MAC and VLAN traffic to divide the traffic into input queues on the basis of the Destination MAC
address, VLAN ID and VLAN user priority fields.
VMDQ filters split the traffic into 16 or 32 groups based on the Destination MAC and VLAN ID.
Then, DCB places each packet into one of queues within that group, based upon the VLAN user priority field.

All traffic is read from a single incoming port (port 0) and output on port 1, without any processing being performed.
With Intel® 82599 NIC, for example, the traffic is split into 128 queues on input, where each thread of the application reads from
multiple queues. When run with 8 threads, that is, with the -c FF option, each thread receives and forwards packets from 16 queues.

As supplied, the sample application configures the VMDQ feature to have 32 pools with 4 queues each as indicated in :numref:`figure_vmdq_dcb_example`.
The Intel® 82599 10 Gigabit Ethernet Controller NIC also supports the splitting of traffic into 16 pools of 8 queues. While the
Intel® X710 or XL710 Ethernet Controller NICs support many configurations of VMDQ pools of 4 or 8 queues each. For simplicity, only 16
or 32 pools is supported in this sample. And queues numbers for each VMDQ pool can be changed by setting RTE_LIBRTE_I40E_QUEUE_NUM_PER_VM
in config/rte_config.h file.
The nb-pools, nb-tcs and enable-rss parameters can be passed on the command line, after the EAL parameters:

.. code-block:: console

    ./<build_dir>/examples/dpdk-vmdq_dcb [EAL options] -- -p PORTMASK --nb-pools NP --nb-tcs TC --enable-rss

where, NP can be 16 or 32, TC can be 4 or 8, rss is disabled by default.

.. _figure_vmdq_dcb_example:

.. figure:: img/vmdq_dcb_example.*

   Packet Flow Through the VMDQ and DCB Sample Application


In Linux* user space, the application can display statistics with the number of packets received on each queue.
To have the application display the statistics, send a SIGHUP signal to the running application process.

The VMDQ and DCB Forwarding sample application is in many ways simpler than the L2 Forwarding application
(see :doc:`l2_forward_real_virtual`)
as it performs unidirectional L2 forwarding of packets from one port to a second port.
No command-line options are taken by this application apart from the standard EAL command-line options.

.. note::

    Since VMD queues are being used for VMM, this application works correctly
    when VTd is disabled in the BIOS or Linux* kernel (intel_iommu=off).

Compiling the Application
-------------------------



To compile the sample application see :doc:`compiling`.

The application is located in the ``vmdq_dcb`` sub-directory.

Running the Application
-----------------------

To run the example in a linux environment:

.. code-block:: console

    user@target:~$ ./<build_dir>/examples/dpdk-vmdq_dcb -l 0-3 -n 4 -- -p 0x3 --nb-pools 32 --nb-tcs 4

Refer to the *DPDK Getting Started Guide* for general information on running applications and
the Environment Abstraction Layer (EAL) options.

Explanation
-----------

The following sections provide some explanation of the code.

Initialization
~~~~~~~~~~~~~~

The EAL, driver and PCI configuration is performed largely as in the L2 Forwarding sample application,
as is the creation of the mbuf pool.
See :doc:`l2_forward_real_virtual`.
Where this example application differs is in the configuration of the NIC port for RX.

The VMDQ and DCB hardware feature is configured at port initialization time by setting the appropriate values in the
rte_eth_conf structure passed to the rte_eth_dev_configure() API.
Initially in the application,
a default structure is provided for VMDQ and DCB configuration to be filled in later by the application.

.. literalinclude:: ../../../examples/vmdq_dcb/main.c
    :language: c
    :start-after: Empty vmdq+dcb configuration structure. Filled in programmatically. 8<
    :end-before: >8 End of empty vmdq+dcb configuration structure.

The get_eth_conf() function fills in an rte_eth_conf structure with the appropriate values,
based on the global vlan_tags array,
and dividing up the possible user priority values equally among the individual queues
(also referred to as traffic classes) within each pool. With Intel® 82599 NIC,
if the number of pools is 32, then the user priority fields are allocated 2 to a queue.
If 16 pools are used, then each of the 8 user priority fields is allocated to its own queue within the pool.
With Intel® X710/XL710 NICs, if number of tcs is 4, and number of queues in pool is 8,
then the user priority fields are allocated 2 to one tc, and a tc has 2 queues mapping to it, then
RSS will determine the destination queue in 2.
For the VLAN IDs, each one can be allocated to possibly multiple pools of queues,
so the pools parameter in the rte_eth_vmdq_dcb_conf structure is specified as a bitmask value.
For destination MAC, each VMDQ pool will be assigned with a MAC address. In this sample, each VMDQ pool
is assigned to the MAC like 52:54:00:12:<port_id>:<pool_id>, that is,
the MAC of VMDQ pool 2 on port 1 is 52:54:00:12:01:02.

.. literalinclude:: ../../../examples/vmdq_dcb/main.c
    :language: c
    :start-after: Dividing up the possible user priority values. 8<
    :end-before: >8 End of dividing up the possible user priority values.

.. literalinclude:: ../../../examples/vmdq_dcb/main.c
    :language: c
    :start-after: Set mac for each pool. 8<
    :end-before: >8 End of set mac for each pool.
    :dedent: 1

Once the network port has been initialized using the correct VMDQ and DCB values,
the initialization of the port's RX and TX hardware rings is performed similarly to that
in the L2 Forwarding sample application.
See :doc:`l2_forward_real_virtual` for more information.

Statistics Display
~~~~~~~~~~~~~~~~~~

When run in a linux environment,
the VMDQ and DCB Forwarding sample application can display statistics showing the number of packets read from each RX queue.
This is provided by way of a signal handler for the SIGHUP signal,
which simply prints to standard output the packet counts in grid form.
Each row of the output is a single pool with the columns being the queue number within that pool.

To generate the statistics output, use the following command:

.. code-block:: console

    user@host$ sudo killall -HUP vmdq_dcb_app

Please note that the statistics output will appear on the terminal where the vmdq_dcb_app is running,
rather than the terminal from which the HUP signal was sent.
