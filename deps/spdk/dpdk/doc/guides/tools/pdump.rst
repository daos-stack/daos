..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2016 Intel Corporation.

.. _pdump_tool:

dpdk-pdump Application
======================

The ``dpdk-pdump`` tool is a Data Plane Development Kit (DPDK) tool that runs as
a DPDK secondary process and is capable of enabling packet capture on dpdk ports.

   .. Note::
      * The ``dpdk-pdump`` tool can only be used in conjunction with a primary
        application which has the packet capture framework initialized already.
        In dpdk, only the ``testpmd`` is modified to initialize packet capture
        framework, other applications remain untouched. So, if the ``dpdk-pdump``
        tool has to be used with any application other than the testpmd, user
        needs to explicitly modify that application to call packet capture
        framework initialization code. Refer ``app/test-pmd/testpmd.c``
        code to see how this is done.

      * The ``dpdk-pdump`` tool depends on DPDK pcap PMD, so the system should
        have libpcap development files installed and the pcap PMD not disabled
        in the build.

      * The ``dpdk-pdump`` tool runs as a DPDK secondary process. It exits when
        the primary application exits.


Running the Application
-----------------------

The tool has a number of command line options:

.. code-block:: console

   ./<build_dir>/app/dpdk-pdump --
                          [--multi]
                          --pdump '(port=<port id> | device_id=<pci id or vdev name>),
                                   (queue=<queue_id>),
                                   (rx-dev=<iface or pcap file> |
                                    tx-dev=<iface or pcap file>),
                                   [ring-size=<ring size>],
                                   [mbuf-size=<mbuf data size>],
                                   [total-num-mbufs=<number of mbufs>]'

The ``--multi`` command line option is optional argument. If passed, capture
will be running on unique cores for all ``--pdump`` options. If ignored,
capture will be running on single core for all ``--pdump`` options.

The ``--pdump`` command line option is mandatory and it takes various sub arguments which are described in
below section.

   .. Note::

      * Parameters inside the parentheses represents mandatory parameters.

      * Parameters inside the square brackets represents optional parameters.

      * Multiple instances of ``--pdump`` can be passed to capture packets on different port and queue combinations.


The ``--pdump`` parameters
~~~~~~~~~~~~~~~~~~~~~~~~~~

``port``:
Port id of the eth device on which packets should be captured.

``device_id``:
PCI address (or) name of the eth device on which packets should be captured.

   .. Note::

      * As of now the ``dpdk-pdump`` tool cannot capture the packets of virtual devices
        in the primary process due to a bug in the ethdev library. Due to this bug, in a multi process context,
        when the primary and secondary have different ports set, then the secondary process
        (here the ``dpdk-pdump`` tool) overwrites the ``rte_eth_devices[]`` entries of the primary process.

``queue``:
Queue id of the eth device on which packets should be captured. The user can pass a queue value of ``*`` to enable
packet capture on all queues of the eth device.

``rx-dev``:
Can be either a pcap file name or any Linux iface.

``tx-dev``:
Can be either a pcap file name or any Linux iface.

   .. Note::

      * To receive ingress packets only, ``rx-dev`` should be passed.

      * To receive egress packets only, ``tx-dev`` should be passed.

      * To receive ingress and egress packets separately ``rx-dev`` and ``tx-dev``
        should both be passed with the different file names or the Linux iface names.

      * To receive ingress and egress packets together, ``rx-dev`` and ``tx-dev``
        should both be passed with the same file name or the same Linux iface name.

``ring-size``:
Size of the ring. This value is used internally for ring creation. The ring will be used to enqueue the packets from
the primary application to the secondary. This is an optional parameter with default size 16384.

``mbuf-size``:
Size of the mbuf data. This is used internally for mempool creation. Ideally this value must be same as
the primary application's mempool's mbuf data size which is used for packet RX. This is an optional parameter with
default size 2176.

``total-num-mbufs``:
Total number mbufs in mempool. This is used internally for mempool creation. This is an optional parameter with default
value 65535.


Example
-------

.. code-block:: console

   $ sudo ./<build_dir>/app/dpdk-pdump -l 3 -- --pdump 'port=0,queue=*,rx-dev=/tmp/rx.pcap'
   $ sudo ./<build_dir>/app/dpdk-pdump -l 3,4,5 -- --multi --pdump 'port=0,queue=*,rx-dev=/tmp/rx-1.pcap' --pdump 'port=1,queue=*,rx-dev=/tmp/rx-2.pcap'
