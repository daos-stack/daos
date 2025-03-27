..  SPDX-License-Identifier: BSD-3-Clause
    Copyright 2015 6WIND S.A.
    Copyright 2015 Mellanox Technologies, Ltd

.. include:: <isonum.txt>

MLX5 poll mode driver
=====================

The MLX5 poll mode driver library (**librte_net_mlx5**) provides support
for **Mellanox ConnectX-4**, **Mellanox ConnectX-4 Lx** , **Mellanox
ConnectX-5**, **Mellanox ConnectX-6**, **Mellanox ConnectX-6 Dx**, **Mellanox
ConnectX-6 Lx**, **Mellanox BlueField** and **Mellanox BlueField-2** families
of 10/25/40/50/100/200 Gb/s adapters as well as their virtual functions (VF)
in SR-IOV context.

Information and documentation about these adapters can be found on the
`Mellanox website <http://www.mellanox.com>`__. Help is also provided by the
`Mellanox community <http://community.mellanox.com/welcome>`__.

There is also a `section dedicated to this poll mode driver
<https://developer.nvidia.com/networking/dpdk>`_.


Design
------

Besides its dependency on libibverbs (that implies libmlx5 and associated
kernel support), librte_net_mlx5 relies heavily on system calls for control
operations such as querying/updating the MTU and flow control parameters.

For security reasons and robustness, this driver only deals with virtual
memory addresses. The way resources allocations are handled by the kernel,
combined with hardware specifications that allow to handle virtual memory
addresses directly, ensure that DPDK applications cannot access random
physical memory (or memory that does not belong to the current process).

This capability allows the PMD to coexist with kernel network interfaces
which remain functional, although they stop receiving unicast packets as
long as they share the same MAC address.
This means legacy linux control tools (for example: ethtool, ifconfig and
more) can operate on the same network interfaces that owned by the DPDK
application.

The PMD can use libibverbs and libmlx5 to access the device firmware
or directly the hardware components.
There are different levels of objects and bypassing abilities
to get the best performances:

- Verbs is a complete high-level generic API
- Direct Verbs is a device-specific API
- DevX allows to access firmware objects
- Direct Rules manages flow steering at low-level hardware layer

Enabling librte_net_mlx5 causes DPDK applications to be linked against
libibverbs.

Features
--------

- Multi arch support: x86_64, POWER8, ARMv8, i686.
- Multiple TX and RX queues.
- Shared Rx queue.
- Rx queue delay drop.
- Support for scattered TX frames.
- Advanced support for scattered Rx frames with tunable buffer attributes.
- IPv4, IPv6, TCPv4, TCPv6, UDPv4 and UDPv6 RSS on any number of queues.
- RSS using different combinations of fields: L3 only, L4 only or both,
  and source only, destination only or both.
- Several RSS hash keys, one for each flow type.
- Default RSS operation with no hash key specification.
- Configurable RETA table.
- Link flow control (pause frame).
- Support for multiple MAC addresses.
- VLAN filtering.
- RX VLAN stripping.
- TX VLAN insertion.
- RX CRC stripping configuration.
- TX mbuf fast free offload.
- Promiscuous mode on PF and VF.
- Multicast promiscuous mode on PF and VF.
- Hardware checksum offloads.
- Flow director (RTE_FDIR_MODE_PERFECT, RTE_FDIR_MODE_PERFECT_MAC_VLAN and
  RTE_ETH_FDIR_REJECT).
- Flow API, including :ref:`flow_isolated_mode`.
- Multiple process.
- KVM and VMware ESX SR-IOV modes are supported.
- RSS hash result is supported.
- Hardware TSO for generic IP or UDP tunnel, including VXLAN and GRE.
- Hardware checksum Tx offload for generic IP or UDP tunnel, including VXLAN and GRE.
- RX interrupts.
- Statistics query including Basic, Extended and per queue.
- Rx HW timestamp.
- Tunnel types: VXLAN, L3 VXLAN, VXLAN-GPE, GRE, MPLSoGRE, MPLSoUDP, IP-in-IP, Geneve, GTP.
- Tunnel HW offloads: packet type, inner/outer RSS, IP and UDP checksum verification.
- NIC HW offloads: encapsulation (vxlan, gre, mplsoudp, mplsogre), NAT, routing, TTL
  increment/decrement, count, drop, mark. For details please see :ref:`mlx5_offloads_support`.
- Flow insertion rate of more then million flows per second, when using Direct Rules.
- Support for multiple rte_flow groups.
- Per packet no-inline hint flag to disable packet data copying into Tx descriptors.
- Hardware LRO.
- Hairpin.
- Multiple-thread flow insertion.
- Matching on IPv4 Internet Header Length (IHL).
- Matching on GTP extension header with raw encap/decap action.
- Matching on Geneve TLV option header with raw encap/decap action.
- RSS support in sample action.
- E-Switch mirroring and jump.
- E-Switch mirroring and modify.
- 21844 flow priorities for ingress or egress flow groups greater than 0 and for any transfer
  flow group.
- Flow metering, including meter policy API.
- Flow meter hierarchy.
- Flow integrity offload API.
- Connection tracking.
- Sub-Function representors.
- Sub-Function.


Limitations
-----------

- Windows support:

  On Windows, the features are limited:

  - Promiscuous mode is not supported
  - The following rules are supported:

    - IPv4/UDP with CVLAN filtering
    - Unicast MAC filtering

  - Additional rules are supported from WinOF2 version 2.70:

    - IPv4/TCP with CVLAN filtering
    - L4 steering rules for port RSS of UDP, TCP and IP

- For secondary process:

  - Forked secondary process not supported.
  - External memory unregistered in EAL memseg list cannot be used for DMA
    unless such memory has been registered by ``mlx5_mr_update_ext_mp()`` in
    primary process and remapped to the same virtual address in secondary
    process. If the external memory is registered by primary process but has
    different virtual address in secondary process, unexpected error may happen.

- Shared Rx queue:

  - Counters of received packets and bytes number of devices in same share group are same.
  - Counters of received packets and bytes number of queues in same group and queue ID are same.

- When using Verbs flow engine (``dv_flow_en`` = 0), flow pattern without any
  specific VLAN will match for VLAN packets as well:

  When VLAN spec is not specified in the pattern, the matching rule will be created with VLAN as a wild card.
  Meaning, the flow rule::

        flow create 0 ingress pattern eth / vlan vid is 3 / ipv4 / end ...

  Will only match vlan packets with vid=3. and the flow rule::

        flow create 0 ingress pattern eth / ipv4 / end ...

  Will match any ipv4 packet (VLAN included).

- When using Verbs flow engine (``dv_flow_en`` = 0), multi-tagged(QinQ) match is not supported.

- When using DV flow engine (``dv_flow_en`` = 1), flow pattern with any VLAN specification will match only single-tagged packets unless the ETH item ``type`` field is 0x88A8 or the VLAN item ``has_more_vlan`` field is 1.
  The flow rule::

        flow create 0 ingress pattern eth / ipv4 / end ...

  Will match any ipv4 packet.
  The flow rules::

        flow create 0 ingress pattern eth / vlan / end ...
        flow create 0 ingress pattern eth has_vlan is 1 / end ...
        flow create 0 ingress pattern eth type is 0x8100 / end ...

  Will match single-tagged packets only, with any VLAN ID value.
  The flow rules::

        flow create 0 ingress pattern eth type is 0x88A8 / end ...
        flow create 0 ingress pattern eth / vlan has_more_vlan is 1 / end ...

  Will match multi-tagged packets only, with any VLAN ID value.

- A flow pattern with 2 sequential VLAN items is not supported.

- VLAN pop offload command:

  - Flow rules having a VLAN pop offload command as one of their actions and
    are lacking a match on VLAN as one of their items are not supported.
  - The command is not supported on egress traffic in NIC mode.

- VLAN push offload is not supported on ingress traffic in NIC mode.

- VLAN set PCP offload is not supported on existing headers.

- A multi segment packet must have not more segments than reported by dev_infos_get()
  in tx_desc_lim.nb_seg_max field. This value depends on maximal supported Tx descriptor
  size and ``txq_inline_min`` settings and may be from 2 (worst case forced by maximal
  inline settings) to 58.

- Match on VXLAN supports the following fields only:

     - VNI
     - Last reserved 8-bits

  Last reserved 8-bits matching is only supported When using DV flow
  engine (``dv_flow_en`` = 1).
  For ConnectX-5, the UDP destination port must be the standard one (4789).
  Group zero's behavior may differ which depends on FW.
  Matching value equals 0 (value & mask) is not supported.

- L3 VXLAN and VXLAN-GPE tunnels cannot be supported together with MPLSoGRE and MPLSoUDP.

- Match on Geneve header supports the following fields only:

     - VNI
     - OAM
     - protocol type
     - options length

- Match on Geneve TLV option is supported on the following fields:

     - Class
     - Type
     - Length
     - Data

  Only one Class/Type/Length Geneve TLV option is supported per shared device.
  Class/Type/Length fields must be specified as well as masks.
  Class/Type/Length specified masks must be full.
  Matching Geneve TLV option without specifying data is not supported.
  Matching Geneve TLV option with ``data & mask == 0`` is not supported.

- VF: flow rules created on VF devices can only match traffic targeted at the
  configured MAC addresses (see ``rte_eth_dev_mac_addr_add()``).

- Match on GTP tunnel header item supports the following fields only:

     - v_pt_rsv_flags: E flag, S flag, PN flag
     - msg_type
     - teid

- Match on GTP extension header only for GTP PDU session container (next
  extension header type = 0x85).
- Match on GTP extension header is not supported in group 0.

- Flex item:

  - Hardware support: BlueField-2.
  - Flex item is supported on PF only.
  - Hardware limits ``header_length_mask_width`` up to 6 bits.
  - Firmware supports 8 global sample fields.
    Each flex item allocates non-shared sample fields from that pool.
  - Supported flex item can have 1 input link - ``eth`` or ``udp``
    and up to 2 output links - ``ipv4`` or ``ipv6``.
  - Flex item fields (``next_header``, ``next_protocol``, ``samples``)
    do not participate in RSS hash functions.
  - In flex item configuration, ``next_header.field_base`` value
    must be byte aligned (multiple of 8).

- No Tx metadata go to the E-Switch steering domain for the Flow group 0.
  The flows within group 0 and set metadata action are rejected by hardware.

.. note::

   MAC addresses not already present in the bridge table of the associated
   kernel network device will be added and cleaned up by the PMD when closing
   the device. In case of ungraceful program termination, some entries may
   remain present and should be removed manually by other means.

- Buffer split offload is supported with regular Rx burst routine only,
  no MPRQ feature or vectorized code can be engaged.

- When Multi-Packet Rx queue is configured (``mprq_en``), a Rx packet can be
  externally attached to a user-provided mbuf with having RTE_MBUF_F_EXTERNAL in
  ol_flags. As the mempool for the external buffer is managed by PMD, all the
  Rx mbufs must be freed before the device is closed. Otherwise, the mempool of
  the external buffers will be freed by PMD and the application which still
  holds the external buffers may be corrupted.

- If Multi-Packet Rx queue is configured (``mprq_en``) and Rx CQE compression is
  enabled (``rxq_cqe_comp_en``) at the same time, RSS hash result is not fully
  supported. Some Rx packets may not have RTE_MBUF_F_RX_RSS_HASH.

- IPv6 Multicast messages are not supported on VM, while promiscuous mode
  and allmulticast mode are both set to off.
  To receive IPv6 Multicast messages on VM, explicitly set the relevant
  MAC address using rte_eth_dev_mac_addr_add() API.

- To support a mixed traffic pattern (some buffers from local host memory, some
  buffers from other devices) with high bandwidth, a mbuf flag is used.

  An application hints the PMD whether or not it should try to inline the
  given mbuf data buffer. PMD should do the best effort to act upon this request.

  The hint flag ``RTE_PMD_MLX5_FINE_GRANULARITY_INLINE`` is dynamic,
  registered by application with rte_mbuf_dynflag_register(). This flag is
  purely driver-specific and declared in PMD specific header ``rte_pmd_mlx5.h``,
  which is intended to be used by the application.

  To query the supported specific flags in runtime,
  the function ``rte_pmd_mlx5_get_dyn_flag_names`` returns the array of
  currently (over present hardware and configuration) supported specific flags.
  The "not inline hint" feature operating flow is the following one:

    - application starts
    - probe the devices, ports are created
    - query the port capabilities
    - if port supporting the feature is found
    - register dynamic flag ``RTE_PMD_MLX5_FINE_GRANULARITY_INLINE``
    - application starts the ports
    - on ``dev_start()`` PMD checks whether the feature flag is registered and
      enables the feature support in datapath
    - application might set the registered flag bit in ``ol_flags`` field
      of mbuf being sent and PMD will handle ones appropriately.

- The amount of descriptors in Tx queue may be limited by data inline settings.
  Inline data require the more descriptor building blocks and overall block
  amount may exceed the hardware supported limits. The application should
  reduce the requested Tx size or adjust data inline settings with
  ``txq_inline_max`` and ``txq_inline_mpw`` devargs keys.

- To provide the packet send scheduling on mbuf timestamps the ``tx_pp``
  parameter should be specified.
  When PMD sees the RTE_MBUF_DYNFLAG_TX_TIMESTAMP_NAME set on the packet
  being sent it tries to synchronize the time of packet appearing on
  the wire with the specified packet timestamp. It the specified one
  is in the past it should be ignored, if one is in the distant future
  it should be capped with some reasonable value (in range of seconds).
  These specific cases ("too late" and "distant future") can be optionally
  reported via device xstats to assist applications to detect the
  time-related problems.

  The timestamp upper "too-distant-future" limit
  at the moment of invoking the Tx burst routine
  can be estimated as ``tx_pp`` option (in nanoseconds) multiplied by 2^23.
  Please note, for the testpmd txonly mode,
  the limit is deduced from the expression::

        (n_tx_descriptors / burst_size + 1) * inter_burst_gap

  There is no any packet reordering according timestamps is supposed,
  neither within packet burst, nor between packets, it is an entirely
  application responsibility to generate packets and its timestamps
  in desired order. The timestamps can be put only in the first packet
  in the burst providing the entire burst scheduling.

- E-Switch decapsulation Flow:

  - can be applied to PF port only.
  - must specify VF port action (packet redirection from PF to VF).
  - optionally may specify tunnel inner source and destination MAC addresses.

- E-Switch  encapsulation Flow:

  - can be applied to VF ports only.
  - must specify PF port action (packet redirection from VF to PF).

- E-Switch Manager matching:

  - For Bluefield with old FW
    which doesn't expose the E-Switch Manager vport ID in the capability,
    matching E-Switch Manager should be used only in Bluefield embedded CPU mode.

- Raw encapsulation:

  - The input buffer, used as outer header, is not validated.

- Raw decapsulation:

  - The decapsulation is always done up to the outermost tunnel detected by the HW.
  - The input buffer, providing the removal size, is not validated.
  - The buffer size must match the length of the headers to be removed.

- ICMP(code/type/identifier/sequence number) / ICMP6(code/type) matching, IP-in-IP and MPLS flow matching are all
  mutually exclusive features which cannot be supported together
  (see :ref:`mlx5_firmware_config`).

- LRO:

  - Requires DevX and DV flow to be enabled.
  - KEEP_CRC offload cannot be supported with LRO.
  - The first mbuf length, without head-room,  must be big enough to include the
    TCP header (122B).
  - Rx queue with LRO offload enabled, receiving a non-LRO packet, can forward
    it with size limited to max LRO size, not to max RX packet length.
  - LRO can be used with outer header of TCP packets of the standard format:
        eth (with or without vlan) / ipv4 or ipv6 / tcp / payload

    Other TCP packets (e.g. with MPLS label) received on Rx queue with LRO enabled, will be received with bad checksum.
  - LRO packet aggregation is performed by HW only for packet size larger than
    ``lro_min_mss_size``. This value is reported on device start, when debug
    mode is enabled.

- CRC:

  - ``RTE_ETH_RX_OFFLOAD_KEEP_CRC`` cannot be supported with decapsulation
    for some NICs (such as ConnectX-6 Dx, ConnectX-6 Lx, and BlueField-2).
    The capability bit ``scatter_fcs_w_decap_disable`` shows NIC support.

- TX mbuf fast free:

  - fast free offload assumes the all mbufs being sent are originated from the
    same memory pool and there is no any extra references to the mbufs (the
    reference counter for each mbuf is equal 1 on tx_burst call). The latter
    means there should be no any externally attached buffers in mbufs. It is
    an application responsibility to provide the correct mbufs if the fast
    free offload is engaged. The mlx5 PMD implicitly produces the mbufs with
    externally attached buffers if MPRQ option is enabled, hence, the fast
    free offload is neither supported nor advertised if there is MPRQ enabled.

- Sample flow:

  - Supports ``RTE_FLOW_ACTION_TYPE_SAMPLE`` action only within NIC Rx and
    E-Switch steering domain.
  - For E-Switch Sampling flow with sample ratio > 1, additional actions are not
    supported in the sample actions list.
  - For ConnectX-5, the ``RTE_FLOW_ACTION_TYPE_SAMPLE`` is typically used as
    first action in the E-Switch egress flow if with header modify or
    encapsulation actions.
  - For NIC Rx flow, supports ``MARK``, ``COUNT``, ``QUEUE``, ``RSS`` in the
    sample actions list.
  - For E-Switch mirroring flow, supports ``RAW ENCAP``, ``Port ID``,
    ``VXLAN ENCAP``, ``NVGRE ENCAP`` in the sample actions list.
  - For ConnectX-5 trusted device, the application metadata with SET_TAG index 0
    is not supported before ``RTE_FLOW_ACTION_TYPE_SAMPLE`` action.

- Modify Field flow:

  - Supports the 'set' operation only for ``RTE_FLOW_ACTION_TYPE_MODIFY_FIELD`` action.
  - Modification of an arbitrary place in a packet via the special ``RTE_FLOW_FIELD_START`` Field ID is not supported.
  - Modification of the 802.1Q Tag, VXLAN Network or GENEVE Network ID's is not supported.
  - Encapsulation levels are not supported, can modify outermost header fields only.
  - Offsets must be 32-bits aligned, cannot skip past the boundary of a field.
  - If the field type is ``RTE_FLOW_FIELD_MAC_TYPE``
    and packet contains one or more VLAN headers,
    the meaningful type field following the last VLAN header
    is used as modify field operation argument.
    The modify field action is not intended to modify VLAN headers type field,
    dedicated VLAN push and pop actions should be used instead.

- IPv6 header item 'proto' field, indicating the next header protocol, should
  not be set as extension header.
  In case the next header is an extension header, it should not be specified in
  IPv6 header item 'proto' field.
  The last extension header item 'next header' field can specify the following
  header protocol type.

- Hairpin:

  - Hairpin between two ports could only manual binding and explicit Tx flow mode. For single port hairpin, all the combinations of auto/manual binding and explicit/implicit Tx flow mode could be supported.
  - Hairpin in switchdev SR-IOV mode is not supported till now.

- Meter:

  - All the meter colors with drop action will be counted only by the global drop statistics.
  - Yellow detection is only supported with ASO metering.
  - Red color must be with drop action.
  - Meter statistics are supported only for drop case.
  - A meter action created with pre-defined policy must be the last action in the flow except single case where the policy actions are:
     - green: NULL or END.
     - yellow: NULL or END.
     - RED: DROP / END.
  - The only supported meter policy actions:
     - green: QUEUE, RSS, PORT_ID, REPRESENTED_PORT, JUMP, DROP, MARK and SET_TAG.
     - yellow: QUEUE, RSS, PORT_ID, REPRESENTED_PORT, JUMP, DROP, MARK and SET_TAG.
     - RED: must be DROP.
  - Policy actions of RSS for green and yellow should have the same configuration except queues.
  - Policy with RSS/queue action is not supported when ``dv_xmeta_en`` enabled.
  - meter profile packet mode is supported.
  - meter profiles of RFC2697, RFC2698 and RFC4115 are supported.

- Integrity:

  - Integrity offload is enabled starting from **ConnectX-6 Dx**.
  - Verification bits provided by the hardware are ``l3_ok``, ``ipv4_csum_ok``, ``l4_ok``, ``l4_csum_ok``.
  - ``level`` value 0 references outer headers.
  - Negative integrity item verification is not supported.
  - Multiple integrity items not supported in a single flow rule.
  - Flow rule items supplied by application must explicitly specify network headers referred by integrity item.
    For example, if integrity item mask sets ``l4_ok`` or ``l4_csum_ok`` bits, reference to L4 network header,
    TCP or UDP, must be in the rule pattern as well::

      flow create 0 ingress pattern integrity level is 0 value mask l3_ok value spec l3_ok / eth / ipv6 / end …

      flow create 0 ingress pattern integrity level is 0 value mask l4_ok value spec l4_ok / eth / ipv4 proto is udp / end …

- Connection tracking:

  - Cannot co-exist with ASO meter, ASO age action in a single flow rule.
  - Flow rules insertion rate and memory consumption need more optimization.
  - 256 ports maximum.
  - 4M connections maximum.

- Multi-thread flow insertion:

  - In order to achieve best insertion rate, application should manage the flows per lcore.
  - Better to disable memory reclaim by setting ``reclaim_mem_mode`` to 0 to accelerate the flow object allocation and release with cache.

- HW hashed bonding

  - TXQ affinity subjects to HW hash once enabled.

- Bonding under socket direct mode

  - Needs OFED 5.4+.

- Timestamps:

  - CQE timestamp field width is limited by hardware to 63 bits, MSB is zero.
  - In the free-running mode the timestamp counter is reset on power on
    and 63-bit value provides over 1800 years of uptime till overflow.
  - In the real-time mode
    (configurable with ``REAL_TIME_CLOCK_ENABLE`` firmware settings),
    the timestamp presents the nanoseconds elapsed since 01-Jan-1970,
    hardware timestamp overflow will happen on 19-Jan-2038
    (0x80000000 seconds since 01-Jan-1970).
  - The send scheduling is based on timestamps
    from the reference "Clock Queue" completions,
    the scheduled send timestamps should not be specified with non-zero MSB.

- The NIC egress flow rules on representor port are not supported.

Statistics
----------

MLX5 supports various methods to report statistics:

Port statistics can be queried using ``rte_eth_stats_get()``. The received and sent statistics are through SW only and counts the number of packets received or sent successfully by the PMD. The imissed counter is the amount of packets that could not be delivered to SW because a queue was full. Packets not received due to congestion in the bus or on the NIC can be queried via the rx_discards_phy xstats counter.

Extended statistics can be queried using ``rte_eth_xstats_get()``. The extended statistics expose a wider set of counters counted by the device. The extended port statistics counts the number of packets received or sent successfully by the port. As Mellanox NICs are using the :ref:`Bifurcated Linux Driver <linux_gsg_linux_drivers>` those counters counts also packet received or sent by the Linux kernel. The counters with ``_phy`` suffix counts the total events on the physical port, therefore not valid for VF.

Finally per-flow statistics can by queried using ``rte_flow_query`` when attaching a count action for specific flow. The flow counter counts the number of packets received successfully by the port and match the specific flow.

Configuration
-------------

Compilation options
~~~~~~~~~~~~~~~~~~~

The ibverbs libraries can be linked with this PMD in a number of ways,
configured by the ``ibverbs_link`` build option:

- ``shared`` (default): the PMD depends on some .so files.

- ``dlopen``: Split the dependencies glue in a separate library
  loaded when needed by dlopen.
  It make dependencies on libibverbs and libmlx4 optional,
  and has no performance impact.

- ``static``: Embed static flavor of the dependencies libibverbs and libmlx4
  in the PMD shared library or the executable static binary.

Environment variables
~~~~~~~~~~~~~~~~~~~~~

- ``MLX5_GLUE_PATH``

  A list of directories in which to search for the rdma-core "glue" plug-in,
  separated by colons or semi-colons.

- ``MLX5_SHUT_UP_BF``

  Configures HW Tx doorbell register as IO-mapped.

  By default, the HW Tx doorbell is configured as a write-combining register.
  The register would be flushed to HW usually when the write-combining buffer
  becomes full, but it depends on CPU design.

Run-time configuration
~~~~~~~~~~~~~~~~~~~~~~

- librte_net_mlx5 brings kernel network interfaces up during initialization
  because it is affected by their state. Forcing them down prevents packets
  reception.

- **ethtool** operations on related kernel interfaces also affect the PMD.

Run as non-root
^^^^^^^^^^^^^^^

In order to run as a non-root user,
some capabilities must be granted to the application::

   setcap cap_sys_admin,cap_net_admin,cap_net_raw,cap_ipc_lock+ep <dpdk-app>

Below are the reasons of the need for each capability:

``cap_sys_admin``
   When using physical addresses (PA mode), with Linux >= 4.0,
   for access to ``/proc/self/pagemap``.

``cap_net_admin``
   For device configuration.

``cap_net_raw``
   For raw ethernet queue allocation through kernel driver.

``cap_ipc_lock``
   For DMA memory pinning.

Driver options
^^^^^^^^^^^^^^

- ``rxq_cqe_comp_en`` parameter [int]

  A nonzero value enables the compression of CQE on RX side. This feature
  allows to save PCI bandwidth and improve performance. Enabled by default.
  Different compression formats are supported in order to achieve the best
  performance for different traffic patterns. Default format depends on
  Multi-Packet Rx queue configuration: Hash RSS format is used in case
  MPRQ is disabled, Checksum format is used in case MPRQ is enabled.

  Specifying 2 as a ``rxq_cqe_comp_en`` value selects Flow Tag format for
  better compression rate in case of RTE Flow Mark traffic.
  Specifying 3 as a ``rxq_cqe_comp_en`` value selects Checksum format.
  Specifying 4 as a ``rxq_cqe_comp_en`` value selects L3/L4 Header format for
  better compression rate in case of mixed TCP/UDP and IPv4/IPv6 traffic.
  CQE compression format selection requires DevX to be enabled. If there is
  no DevX enabled/supported the value is reset to 1 by default.

  Supported on:

  - x86_64 with ConnectX-4, ConnectX-4 Lx, ConnectX-5, ConnectX-6, ConnectX-6 Dx,
    ConnectX-6 Lx, BlueField and BlueField-2.
  - POWER9 and ARMv8 with ConnectX-4 Lx, ConnectX-5, ConnectX-6, ConnectX-6 Dx,
    ConnectX-6 Lx, BlueField and BlueField-2.

- ``rxq_pkt_pad_en`` parameter [int]

  A nonzero value enables padding Rx packet to the size of cacheline on PCI
  transaction. This feature would waste PCI bandwidth but could improve
  performance by avoiding partial cacheline write which may cause costly
  read-modify-copy in memory transaction on some architectures. Disabled by
  default.

  Supported on:

  - x86_64 with ConnectX-4, ConnectX-4 Lx, ConnectX-5, ConnectX-6, ConnectX-6 Dx,
    ConnectX-6 Lx, BlueField and BlueField-2.
  - POWER8 and ARMv8 with ConnectX-4 Lx, ConnectX-5, ConnectX-6, ConnectX-6 Dx,
    ConnectX-6 Lx, BlueField and BlueField-2.

- ``delay_drop`` parameter [int]

  Bitmask value for the Rx queue delay drop attribute. Bit 0 is used for the
  standard Rx queue and bit 1 is used for the hairpin Rx queue. By default, the
  delay drop is disabled for all Rx queues. It will be ignored if the port does
  not support the attribute even if it is enabled explicitly.

  The packets being received will not be dropped immediately when the WQEs are
  exhausted in a Rx queue with delay drop enabled.

  A timeout value is set in the driver to control the waiting time before
  dropping a packet. Once the timer is expired, the delay drop will be
  deactivated for all the Rx queues with this feature enable. To re-activate
  it, a rearming is needed and it is part of the kernel driver starting from
  OFED 5.5.

  To enable / disable the delay drop rearming, the private flag ``dropless_rq``
  can be set and queried via ethtool:

  - ethtool --set-priv-flags <netdev> dropless_rq on (/ off)
  - ethtool --show-priv-flags <netdev>

  The configuration flag is global per PF and can only be set on the PF, once
  it is on, all the VFs', SFs' and representors' Rx queues will share the timer
  and rearming.

- ``mprq_en`` parameter [int]

  A nonzero value enables configuring Multi-Packet Rx queues. Rx queue is
  configured as Multi-Packet RQ if the total number of Rx queues is
  ``rxqs_min_mprq`` or more. Disabled by default.

  Multi-Packet Rx Queue (MPRQ a.k.a Striding RQ) can further save PCIe bandwidth
  by posting a single large buffer for multiple packets. Instead of posting a
  buffers per a packet, one large buffer is posted in order to receive multiple
  packets on the buffer. A MPRQ buffer consists of multiple fixed-size strides
  and each stride receives one packet. MPRQ can improve throughput for
  small-packet traffic.

  When MPRQ is enabled, MTU can be larger than the size of
  user-provided mbuf even if RTE_ETH_RX_OFFLOAD_SCATTER isn't enabled. PMD will
  configure large stride size enough to accommodate MTU as long as
  device allows. Note that this can waste system memory compared to enabling Rx
  scatter and multi-segment packet.

- ``mprq_log_stride_num`` parameter [int]

  Log 2 of the number of strides for Multi-Packet Rx queue. Configuring more
  strides can reduce PCIe traffic further. If configured value is not in the
  range of device capability, the default value will be set with a warning
  message. The default value is 4 which is 16 strides per a buffer, valid only
  if ``mprq_en`` is set.

  The size of Rx queue should be bigger than the number of strides.

- ``mprq_log_stride_size`` parameter [int]

  Log 2 of the size of a stride for Multi-Packet Rx queue. Configuring a smaller
  stride size can save some memory and reduce probability of a depletion of all
  available strides due to unreleased packets by an application. If configured
  value is not in the range of device capability, the default value will be set
  with a warning message. The default value is 11 which is 2048 bytes per a
  stride, valid only if ``mprq_en`` is set. With ``mprq_log_stride_size`` set
  it is possible for a packet to span across multiple strides. This mode allows
  support of jumbo frames (9K) with MPRQ. The memcopy of some packets (or part
  of a packet if Rx scatter is configured) may be required in case there is no
  space left for a head room at the end of a stride which incurs some
  performance penalty.

- ``mprq_max_memcpy_len`` parameter [int]

  The maximum length of packet to memcpy in case of Multi-Packet Rx queue. Rx
  packet is mem-copied to a user-provided mbuf if the size of Rx packet is less
  than or equal to this parameter. Otherwise, PMD will attach the Rx packet to
  the mbuf by external buffer attachment - ``rte_pktmbuf_attach_extbuf()``.
  A mempool for external buffers will be allocated and managed by PMD. If Rx
  packet is externally attached, ol_flags field of the mbuf will have
  RTE_MBUF_F_EXTERNAL and this flag must be preserved. ``RTE_MBUF_HAS_EXTBUF()``
  checks the flag. The default value is 128, valid only if ``mprq_en`` is set.

- ``rxqs_min_mprq`` parameter [int]

  Configure Rx queues as Multi-Packet RQ if the total number of Rx queues is
  greater or equal to this value. The default value is 12, valid only if
  ``mprq_en`` is set.

- ``txq_inline`` parameter [int]

  Amount of data to be inlined during TX operations. This parameter is
  deprecated and converted to the new parameter ``txq_inline_max`` providing
  partial compatibility.

- ``txqs_min_inline`` parameter [int]

  Enable inline data send only when the number of TX queues is greater or equal
  to this value.

  This option should be used in combination with ``txq_inline_max`` and
  ``txq_inline_mpw`` below and does not affect ``txq_inline_min`` settings above.

  If this option is not specified the default value 16 is used for BlueField
  and 8 for other platforms

  The data inlining consumes the CPU cycles, so this option is intended to
  auto enable inline data if we have enough Tx queues, which means we have
  enough CPU cores and PCI bandwidth is getting more critical and CPU
  is not supposed to be bottleneck anymore.

  The copying data into WQE improves latency and can improve PPS performance
  when PCI back pressure is detected and may be useful for scenarios involving
  heavy traffic on many queues.

  Because additional software logic is necessary to handle this mode, this
  option should be used with care, as it may lower performance when back
  pressure is not expected.

  If inline data are enabled it may affect the maximal size of Tx queue in
  descriptors because the inline data increase the descriptor size and
  queue size limits supported by hardware may be exceeded.

- ``txq_inline_min`` parameter [int]

  Minimal amount of data to be inlined into WQE during Tx operations. NICs
  may require this minimal data amount to operate correctly. The exact value
  may depend on NIC operation mode, requested offloads, etc. It is strongly
  recommended to omit this parameter and use the default values. Anyway,
  applications using this parameter should take into consideration that
  specifying an inconsistent value may prevent the NIC from sending packets.

  If ``txq_inline_min`` key is present the specified value (may be aligned
  by the driver in order not to exceed the limits and provide better descriptor
  space utilization) will be used by the driver and it is guaranteed that
  requested amount of data bytes are inlined into the WQE beside other inline
  settings. This key also may update ``txq_inline_max`` value (default
  or specified explicitly in devargs) to reserve the space for inline data.

  If ``txq_inline_min`` key is not present, the value may be queried by the
  driver from the NIC via DevX if this feature is available. If there is no DevX
  enabled/supported the value 18 (supposing L2 header including VLAN) is set
  for ConnectX-4 and ConnectX-4 Lx, and 0 is set by default for ConnectX-5
  and newer NICs. If packet is shorter the ``txq_inline_min`` value, the entire
  packet is inlined.

  For ConnectX-4 NIC, driver does not allow specifying value below 18
  (minimal L2 header, including VLAN), error will be raised.

  For ConnectX-4 Lx NIC, it is allowed to specify values below 18, but
  it is not recommended and may prevent NIC from sending packets over
  some configurations.

  For ConnectX-4 and ConnectX-4 Lx NICs, automatically configured value
  is insufficient for some traffic, because they require at least all L2 headers
  to be inlined. For example, Q-in-Q adds 4 bytes to default 18 bytes
  of Ethernet and VLAN, thus ``txq_inline_min`` must be set to 22.
  MPLS would add 4 bytes per label. Final value must account for all possible
  L2 encapsulation headers used in particular environment.

  Please, note, this minimal data inlining disengages eMPW feature (Enhanced
  Multi-Packet Write), because last one does not support partial packet inlining.
  This is not very critical due to minimal data inlining is mostly required
  by ConnectX-4 and ConnectX-4 Lx, these NICs do not support eMPW feature.

- ``txq_inline_max`` parameter [int]

  Specifies the maximal packet length to be completely inlined into WQE
  Ethernet Segment for ordinary SEND method. If packet is larger than specified
  value, the packet data won't be copied by the driver at all, data buffer
  is addressed with a pointer. If packet length is less or equal all packet
  data will be copied into WQE. This may improve PCI bandwidth utilization for
  short packets significantly but requires the extra CPU cycles.

  The data inline feature is controlled by number of Tx queues, if number of Tx
  queues is larger than ``txqs_min_inline`` key parameter, the inline feature
  is engaged, if there are not enough Tx queues (which means not enough CPU cores
  and CPU resources are scarce), data inline is not performed by the driver.
  Assigning ``txqs_min_inline`` with zero always enables the data inline.

  The default ``txq_inline_max`` value is 290. The specified value may be adjusted
  by the driver in order not to exceed the limit (930 bytes) and to provide better
  WQE space filling without gaps, the adjustment is reflected in the debug log.
  Also, the default value (290) may be decreased in run-time if the large transmit
  queue size is requested and hardware does not support enough descriptor
  amount, in this case warning is emitted. If ``txq_inline_max`` key is
  specified and requested inline settings can not be satisfied then error
  will be raised.

- ``txq_inline_mpw`` parameter [int]

  Specifies the maximal packet length to be completely inlined into WQE for
  Enhanced MPW method. If packet is large the specified value, the packet data
  won't be copied, and data buffer is addressed with pointer. If packet length
  is less or equal, all packet data will be copied into WQE. This may improve PCI
  bandwidth utilization for short packets significantly but requires the extra
  CPU cycles.

  The data inline feature is controlled by number of TX queues, if number of Tx
  queues is larger than ``txqs_min_inline`` key parameter, the inline feature
  is engaged, if there are not enough Tx queues (which means not enough CPU cores
  and CPU resources are scarce), data inline is not performed by the driver.
  Assigning ``txqs_min_inline`` with zero always enables the data inline.

  The default ``txq_inline_mpw`` value is 268. The specified value may be adjusted
  by the driver in order not to exceed the limit (930 bytes) and to provide better
  WQE space filling without gaps, the adjustment is reflected in the debug log.
  Due to multiple packets may be included to the same WQE with Enhanced Multi
  Packet Write Method and overall WQE size is limited it is not recommended to
  specify large values for the ``txq_inline_mpw``. Also, the default value (268)
  may be decreased in run-time if the large transmit queue size is requested
  and hardware does not support enough descriptor amount, in this case warning
  is emitted. If ``txq_inline_mpw`` key is  specified and requested inline
  settings can not be satisfied then error will be raised.

- ``txqs_max_vec`` parameter [int]

  Enable vectorized Tx only when the number of TX queues is less than or
  equal to this value. This parameter is deprecated and ignored, kept
  for compatibility issue to not prevent driver from probing.

- ``txq_mpw_hdr_dseg_en`` parameter [int]

  A nonzero value enables including two pointers in the first block of TX
  descriptor. The parameter is deprecated and ignored, kept for compatibility
  issue.

- ``txq_max_inline_len`` parameter [int]

  Maximum size of packet to be inlined. This limits the size of packet to
  be inlined. If the size of a packet is larger than configured value, the
  packet isn't inlined even though there's enough space remained in the
  descriptor. Instead, the packet is included with pointer. This parameter
  is deprecated and converted directly to ``txq_inline_mpw`` providing full
  compatibility. Valid only if eMPW feature is engaged.

- ``txq_mpw_en`` parameter [int]

  A nonzero value enables Enhanced Multi-Packet Write (eMPW) for ConnectX-5,
  ConnectX-6, ConnectX-6 Dx, ConnectX-6 Lx, BlueField, BlueField-2.
  eMPW allows the Tx burst function to pack up multiple packets
  in a single descriptor session in order to save PCI bandwidth
  and improve performance at the cost of a slightly higher CPU usage.
  When ``txq_inline_mpw`` is set along with ``txq_mpw_en``,
  Tx burst function copies entire packet data on to Tx descriptor
  instead of including pointer of packet.

  The Enhanced Multi-Packet Write feature is enabled by default if NIC supports
  it, can be disabled by explicit specifying 0 value for ``txq_mpw_en`` option.
  Also, if minimal data inlining is requested by non-zero ``txq_inline_min``
  option or reported by the NIC, the eMPW feature is disengaged.

- ``tx_db_nc`` parameter [int]

  The rdma core library can map doorbell register in two ways, depending on the
  environment variable "MLX5_SHUT_UP_BF":

  - As regular cached memory (usually with write combining attribute), if the
    variable is either missing or set to zero.
  - As non-cached memory, if the variable is present and set to not "0" value.

  The type of mapping may slightly affect the Tx performance, the optimal choice
  is strongly relied on the host architecture and should be deduced practically.

  If ``tx_db_nc`` is set to zero, the doorbell is forced to be mapped to regular
  memory (with write combining), the PMD will perform the extra write memory barrier
  after writing to doorbell, it might increase the needed CPU clocks per packet
  to send, but latency might be improved.

  If ``tx_db_nc`` is set to one, the doorbell is forced to be mapped to non
  cached memory, the PMD will not perform the extra write memory barrier
  after writing to doorbell, on some architectures it might improve the
  performance.

  If ``tx_db_nc`` is set to two, the doorbell is forced to be mapped to regular
  memory, the PMD will use heuristics to decide whether write memory barrier
  should be performed. For bursts with size multiple of recommended one (64 pkts)
  it is supposed the next burst is coming and no need to issue the extra memory
  barrier (it is supposed to be issued in the next coming burst, at least after
  descriptor writing). It might increase latency (on some hosts till next
  packets transmit) and should be used with care.

  If ``tx_db_nc`` is omitted or set to zero, the preset (if any) environment
  variable "MLX5_SHUT_UP_BF" value is used. If there is no "MLX5_SHUT_UP_BF",
  the default ``tx_db_nc`` value is zero for ARM64 hosts and one for others.

- ``tx_pp`` parameter [int]

  If a nonzero value is specified the driver creates all necessary internal
  objects to provide accurate packet send scheduling on mbuf timestamps.
  The positive value specifies the scheduling granularity in nanoseconds,
  the packet send will be accurate up to specified digits. The allowed range is
  from 500 to 1 million of nanoseconds. The negative value specifies the module
  of granularity and engages the special test mode the check the schedule rate.
  By default (if the ``tx_pp`` is not specified) send scheduling on timestamps
  feature is disabled.

- ``tx_skew`` parameter [int]

  The parameter adjusts the send packet scheduling on timestamps and represents
  the average delay between beginning of the transmitting descriptor processing
  by the hardware and appearance of actual packet data on the wire. The value
  should be provided in nanoseconds and is valid only if ``tx_pp`` parameter is
  specified. The default value is zero.

- ``tx_vec_en`` parameter [int]

  A nonzero value enables Tx vector on ConnectX-5, ConnectX-6, ConnectX-6 Dx,
  ConnectX-6 Lx, BlueField and BlueField-2 NICs
  if the number of global Tx queues on the port is less than ``txqs_max_vec``.
  The parameter is deprecated and ignored.

- ``rx_vec_en`` parameter [int]

  A nonzero value enables Rx vector if the port is not configured in
  multi-segment otherwise this parameter is ignored.

  Enabled by default.

- ``vf_nl_en`` parameter [int]

  A nonzero value enables Netlink requests from the VF to add/remove MAC
  addresses or/and enable/disable promiscuous/all multicast on the Netdevice.
  Otherwise the relevant configuration must be run with Linux iproute2 tools.
  This is a prerequisite to receive this kind of traffic.

  Enabled by default, valid only on VF devices ignored otherwise.

- ``l3_vxlan_en`` parameter [int]

  A nonzero value allows L3 VXLAN and VXLAN-GPE flow creation. To enable
  L3 VXLAN or VXLAN-GPE, users has to configure firmware and enable this
  parameter. This is a prerequisite to receive this kind of traffic.

  Disabled by default.

- ``dv_xmeta_en`` parameter [int]

  A nonzero value enables extensive flow metadata support if device is
  capable and driver supports it. This can enable extensive support of
  ``MARK`` and ``META`` item of ``rte_flow``. The newly introduced
  ``SET_TAG`` and ``SET_META`` actions do not depend on ``dv_xmeta_en``.

  There are some possible configurations, depending on parameter value:

  - 0, this is default value, defines the legacy mode, the ``MARK`` and
    ``META`` related actions and items operate only within NIC Tx and
    NIC Rx steering domains, no ``MARK`` and ``META`` information crosses
    the domain boundaries. The ``MARK`` item is 24 bits wide, the ``META``
    item is 32 bits wide and match supported on egress only.

  - 1, this engages extensive metadata mode, the ``MARK`` and ``META``
    related actions and items operate within all supported steering domains,
    including FDB, ``MARK`` and ``META`` information may cross the domain
    boundaries. The ``MARK`` item is 24 bits wide, the ``META`` item width
    depends on kernel and firmware configurations and might be 0, 16 or
    32 bits. Within NIC Tx domain ``META`` data width is 32 bits for
    compatibility, the actual width of data transferred to the FDB domain
    depends on kernel configuration and may be vary. The actual supported
    width can be retrieved in runtime by series of rte_flow_validate()
    trials.

  - 2, this engages extensive metadata mode, the ``MARK`` and ``META``
    related actions and items operate within all supported steering domains,
    including FDB, ``MARK`` and ``META`` information may cross the domain
    boundaries. The ``META`` item is 32 bits wide, the ``MARK`` item width
    depends on kernel and firmware configurations and might be 0, 16 or
    24 bits. The actual supported width can be retrieved in runtime by
    series of rte_flow_validate() trials.

  - 3, this engages tunnel offload mode. In E-Switch configuration, that
    mode implicitly activates ``dv_xmeta_en=1``.

  +------+-----------+-----------+-------------+-------------+
  | Mode | ``MARK``  | ``META``  | ``META`` Tx | FDB/Through |
  +======+===========+===========+=============+=============+
  | 0    | 24 bits   | 32 bits   | 32 bits     | no          |
  +------+-----------+-----------+-------------+-------------+
  | 1    | 24 bits   | vary 0-32 | 32 bits     | yes         |
  +------+-----------+-----------+-------------+-------------+
  | 2    | vary 0-24 | 32 bits   | 32 bits     | yes         |
  +------+-----------+-----------+-------------+-------------+

  If there is no E-Switch configuration the ``dv_xmeta_en`` parameter is
  ignored and the device is configured to operate in legacy mode (0).

  Disabled by default (set to 0).

  The Direct Verbs/Rules (engaged with ``dv_flow_en`` = 1) supports all
  of the extensive metadata features. The legacy Verbs supports FLAG and
  MARK metadata actions over NIC Rx steering domain only.

  Setting META value to zero in flow action means there is no item provided
  and receiving datapath will not report in mbufs the metadata are present.
  Setting MARK value to zero in flow action means the zero FDIR ID value
  will be reported on packet receiving.

  For the MARK action the last 16 values in the full range are reserved for
  internal PMD purposes (to emulate FLAG action). The valid range for the
  MARK action values is 0-0xFFEF for the 16-bit mode and 0-0xFFFFEF
  for the 24-bit mode, the flows with the MARK action value outside
  the specified range will be rejected.

- ``dv_flow_en`` parameter [int]

  A nonzero value enables the DV flow steering assuming it is supported
  by the driver (RDMA Core library version is rdma-core-24.0 or higher).

  Enabled by default if supported.

- ``dv_esw_en`` parameter [int]

  A nonzero value enables E-Switch using Direct Rules.

  Enabled by default if supported.

- ``lacp_by_user`` parameter [int]

  A nonzero value enables the control of LACP traffic by the user application.
  When a bond exists in the driver, by default it should be managed by the
  kernel and therefore LACP traffic should be steered to the kernel.
  If this devarg is set to 1 it will allow the user to manage the bond by
  itself and not steer LACP traffic to the kernel.

  Disabled by default (set to 0).

- ``mr_ext_memseg_en`` parameter [int]

  A nonzero value enables extending memseg when registering DMA memory. If
  enabled, the number of entries in MR (Memory Region) lookup table on datapath
  is minimized and it benefits performance. On the other hand, it worsens memory
  utilization because registered memory is pinned by kernel driver. Even if a
  page in the extended chunk is freed, that doesn't become reusable until the
  entire memory is freed.

  Enabled by default.

- ``mr_mempool_reg_en`` parameter [int]

  A nonzero value enables implicit registration of DMA memory of all mempools
  except those having ``RTE_MEMPOOL_F_NON_IO``. This flag is set automatically
  for mempools populated with non-contiguous objects or those without IOVA.
  The effect is that when a packet from a mempool is transmitted,
  its memory is already registered for DMA in the PMD and no registration
  will happen on the data path. The tradeoff is extra work on the creation
  of each mempool and increased HW resource use if some mempools
  are not used with MLX5 devices.

  Enabled by default.

- ``representor`` parameter [list]

  This parameter can be used to instantiate DPDK Ethernet devices from
  existing port (PF, VF or SF) representors configured on the device.

  It is a standard parameter whose format is described in
  :ref:`ethernet_device_standard_device_arguments`.

  For instance, to probe VF port representors 0 through 2::

    <PCI_BDF>,representor=vf[0-2]

  To probe SF port representors 0 through 2::

    <PCI_BDF>,representor=sf[0-2]

  To probe VF port representors 0 through 2 on both PFs of bonding device::

    <Primary_PCI_BDF>,representor=pf[0,1]vf[0-2]

- ``max_dump_files_num`` parameter [int]

  The maximum number of files per PMD entity that may be created for debug information.
  The files will be created in /var/log directory or in current directory.

  set to 128 by default.

- ``lro_timeout_usec`` parameter [int]

  The maximum allowed duration of an LRO session, in micro-seconds.
  PMD will set the nearest value supported by HW, which is not bigger than
  the input ``lro_timeout_usec`` value.
  If this parameter is not specified, by default PMD will set
  the smallest value supported by HW.

- ``hp_buf_log_sz`` parameter [int]

  The total data buffer size of a hairpin queue (logarithmic form), in bytes.
  PMD will set the data buffer size to 2 ** ``hp_buf_log_sz``, both for RX & TX.
  The capacity of the value is specified by the firmware and the initialization
  will get a failure if it is out of scope.
  The range of the value is from 11 to 19 right now, and the supported frame
  size of a single packet for hairpin is from 512B to 128KB. It might change if
  different firmware release is being used. By using a small value, it could
  reduce memory consumption but not work with a large frame. If the value is
  too large, the memory consumption will be high and some potential performance
  degradation will be introduced.
  By default, the PMD will set this value to 16, which means that 9KB jumbo
  frames will be supported.

- ``reclaim_mem_mode`` parameter [int]

  Cache some resources in flow destroy will help flow recreation more efficient.
  While some systems may require the all the resources can be reclaimed after
  flow destroyed.
  The parameter ``reclaim_mem_mode`` provides the option for user to configure
  if the resource cache is needed or not.

  There are three options to choose:

  - 0. It means the flow resources will be cached as usual. The resources will
    be cached, helpful with flow insertion rate.

  - 1. It will only enable the DPDK PMD level resources reclaim.

  - 2. Both DPDK PMD level and rdma-core low level will be configured as
    reclaimed mode.

  By default, the PMD will set this value to 0.

- ``sys_mem_en`` parameter [int]

  A non-zero value enables the PMD memory management allocating memory
  from system by default, without explicit rte memory flag.

  By default, the PMD will set this value to 0.

- ``decap_en`` parameter [int]

  Some devices do not support FCS (frame checksum) scattering for
  tunnel-decapsulated packets.
  If set to 0, this option forces the FCS feature and rejects tunnel
  decapsulation in the flow engine for such devices.

  By default, the PMD will set this value to 1.

- ``allow_duplicate_pattern`` parameter [int]

  There are two options to choose:

  - 0. Prevent insertion of rules with the same pattern items on non-root table.
    In this case, only the first rule is inserted and the following rules are
    rejected and error code EEXIST is returned.

  - 1. Allow insertion of rules with the same pattern items.
    In this case, all rules are inserted but only the first rule takes effect,
    the next rule takes effect only if the previous rules are deleted.

  By default, the PMD will set this value to 1.

.. _mlx5_firmware_config:

Firmware configuration
~~~~~~~~~~~~~~~~~~~~~~

Firmware features can be configured as key/value pairs.

The command to set a value is::

  mlxconfig -d <device> set <key>=<value>

The command to query a value is::

  mlxconfig -d <device> query | grep <key>

The device name for the command ``mlxconfig`` can be either the PCI address,
or the mst device name found with::

  mst status

Below are some firmware configurations listed.

- link type::

    LINK_TYPE_P1
    LINK_TYPE_P2
    value: 1=Infiniband 2=Ethernet 3=VPI(auto-sense)

- enable SR-IOV::

    SRIOV_EN=1

- maximum number of SR-IOV virtual functions::

    NUM_OF_VFS=<max>

- enable DevX (required by Direct Rules and other features)::

    UCTX_EN=1

- aggressive CQE zipping::

    CQE_COMPRESSION=1

- L3 VXLAN and VXLAN-GPE destination UDP port::

    IP_OVER_VXLAN_EN=1
    IP_OVER_VXLAN_PORT=<udp dport>

- enable VXLAN-GPE tunnel flow matching::

    FLEX_PARSER_PROFILE_ENABLE=0
    or
    FLEX_PARSER_PROFILE_ENABLE=2

- enable IP-in-IP tunnel flow matching::

    FLEX_PARSER_PROFILE_ENABLE=0

- enable MPLS flow matching::

    FLEX_PARSER_PROFILE_ENABLE=1

- enable ICMP(code/type/identifier/sequence number) / ICMP6(code/type) fields matching::

    FLEX_PARSER_PROFILE_ENABLE=2

- enable Geneve flow matching::

   FLEX_PARSER_PROFILE_ENABLE=0
   or
   FLEX_PARSER_PROFILE_ENABLE=1

- enable Geneve TLV option flow matching::

   FLEX_PARSER_PROFILE_ENABLE=0

- enable GTP flow matching::

   FLEX_PARSER_PROFILE_ENABLE=3

- enable eCPRI flow matching::

   FLEX_PARSER_PROFILE_ENABLE=4
   PROG_PARSE_GRAPH=1

- enable dynamic flex parser for flex item::

   FLEX_PARSER_PROFILE_ENABLE=4
   PROG_PARSE_GRAPH=1

- enable realtime timestamp format::

   REAL_TIME_CLOCK_ENABLE=1

Linux Prerequisites
-------------------

This driver relies on external libraries and kernel drivers for resources
allocations and initialization. The following dependencies are not part of
DPDK and must be installed separately:

- **libibverbs**

  User space Verbs framework used by librte_net_mlx5. This library provides
  a generic interface between the kernel and low-level user space drivers
  such as libmlx5.

  It allows slow and privileged operations (context initialization, hardware
  resources allocations) to be managed by the kernel and fast operations to
  never leave user space.

- **libmlx5**

  Low-level user space driver library for Mellanox
  ConnectX-4/ConnectX-5/ConnectX-6/BlueField devices, it is automatically loaded
  by libibverbs.

  This library basically implements send/receive calls to the hardware
  queues.

- **Kernel modules**

  They provide the kernel-side Verbs API and low level device drivers that
  manage actual hardware initialization and resources sharing with user
  space processes.

  Unlike most other PMDs, these modules must remain loaded and bound to
  their devices:

  - mlx5_core: hardware driver managing Mellanox
    ConnectX-4/ConnectX-5/ConnectX-6/BlueField devices and related Ethernet kernel
    network devices.
  - mlx5_ib: InfiniBand device driver.
  - ib_uverbs: user space driver for Verbs (entry point for libibverbs).

- **Firmware update**

  Mellanox OFED/EN releases include firmware updates for
  ConnectX-4/ConnectX-5/ConnectX-6/BlueField adapters.

  Because each release provides new features, these updates must be applied to
  match the kernel modules and libraries they come with.

.. note::

   Both libraries are BSD and GPL licensed. Linux kernel modules are GPL
   licensed.

Installation
~~~~~~~~~~~~

Either RDMA Core library with a recent enough Linux kernel release
(recommended) or Mellanox OFED/EN, which provides compatibility with older
releases.

RDMA Core with Linux Kernel
^^^^^^^^^^^^^^^^^^^^^^^^^^^

- Minimal kernel version : v4.14 or the most recent 4.14-rc (see `Linux installation documentation`_)
- Minimal rdma-core version: v15+ commit 0c5f5765213a ("Merge pull request #227 from yishaih/tm")
  (see `RDMA Core installation documentation`_)
- When building for i686 use:

  - rdma-core version 18.0 or above built with 32bit support.
  - Kernel version 4.14.41 or above.

- Starting with rdma-core v21, static libraries can be built::

    cd build
    CFLAGS=-fPIC cmake -DIN_PLACE=1 -DENABLE_STATIC=1 -GNinja ..
    ninja

.. _`Linux installation documentation`: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git/plain/Documentation/admin-guide/README.rst
.. _`RDMA Core installation documentation`: https://raw.githubusercontent.com/linux-rdma/rdma-core/master/README.md


Mellanox OFED/EN
^^^^^^^^^^^^^^^^

- Mellanox OFED version: **4.5** and above /
  Mellanox EN version: **4.5** and above
- firmware version:

  - ConnectX-4: **12.21.1000** and above.
  - ConnectX-4 Lx: **14.21.1000** and above.
  - ConnectX-5: **16.21.1000** and above.
  - ConnectX-5 Ex: **16.21.1000** and above.
  - ConnectX-6: **20.27.0090** and above.
  - ConnectX-6 Dx: **22.27.0090** and above.
  - BlueField: **18.25.1010** and above.

While these libraries and kernel modules are available on OpenFabrics
Alliance's `website <https://www.openfabrics.org/>`__ and provided by package
managers on most distributions, this PMD requires Ethernet extensions that
may not be supported at the moment (this is a work in progress).

`Mellanox OFED
<https://network.nvidia.com/products/infiniband-drivers/linux/mlnx_ofed/>`__ and
`Mellanox EN
<https://network.nvidia.com/products/ethernet-drivers/linux/mlnx_en/>`__
include the necessary support and should be used in the meantime. For DPDK,
only libibverbs, libmlx5, mlnx-ofed-kernel packages and firmware updates are
required from that distribution.

.. note::

   Several versions of Mellanox OFED/EN are available. Installing the version
   this DPDK release was developed and tested against is strongly
   recommended. Please check the `linux prerequisites`_.

Windows Prerequisites
---------------------

This driver relies on external libraries and kernel drivers for resources
allocations and initialization. The dependencies in the following sub-sections
are not part of DPDK, and must be installed separately.

Compilation Prerequisites
~~~~~~~~~~~~~~~~~~~~~~~~~

DevX SDK installation
^^^^^^^^^^^^^^^^^^^^^

The DevX SDK must be installed on the machine building the Windows PMD.
Additional information can be found at
`How to Integrate Windows DevX in Your Development Environment
<https://docs.mellanox.com/display/winof2v250/RShim+Drivers+and+Usage#RShimDriversandUsage-DevXInterface>`__.

Runtime Prerequisites
~~~~~~~~~~~~~~~~~~~~~

WinOF2 version 2.60 or higher must be installed on the machine.

WinOF2 installation
^^^^^^^^^^^^^^^^^^^

The driver can be downloaded from the following site:
`WINOF2
<https://www.mellanox.com/products/adapter-software/ethernet/windows/winof-2>`__

DevX Enablement
^^^^^^^^^^^^^^^

DevX for Windows must be enabled in the Windows registry.
The keys ``DevxEnabled`` and ``DevxFsRules`` must be set.
Additional information can be found in the WinOF2 user manual.

Supported NICs
--------------

The following Mellanox device families are supported by the same mlx5 driver:

  - ConnectX-4
  - ConnectX-4 Lx
  - ConnectX-5
  - ConnectX-5 Ex
  - ConnectX-6
  - ConnectX-6 Dx
  - ConnectX-6 Lx
  - BlueField
  - BlueField-2

Below are detailed device names:

* Mellanox\ |reg| ConnectX\ |reg|-4 10G MCX4111A-XCAT (1x10G)
* Mellanox\ |reg| ConnectX\ |reg|-4 10G MCX412A-XCAT (2x10G)
* Mellanox\ |reg| ConnectX\ |reg|-4 25G MCX4111A-ACAT (1x25G)
* Mellanox\ |reg| ConnectX\ |reg|-4 25G MCX412A-ACAT (2x25G)
* Mellanox\ |reg| ConnectX\ |reg|-4 40G MCX413A-BCAT (1x40G)
* Mellanox\ |reg| ConnectX\ |reg|-4 40G MCX4131A-BCAT (1x40G)
* Mellanox\ |reg| ConnectX\ |reg|-4 40G MCX415A-BCAT (1x40G)
* Mellanox\ |reg| ConnectX\ |reg|-4 50G MCX413A-GCAT (1x50G)
* Mellanox\ |reg| ConnectX\ |reg|-4 50G MCX4131A-GCAT (1x50G)
* Mellanox\ |reg| ConnectX\ |reg|-4 50G MCX414A-BCAT (2x50G)
* Mellanox\ |reg| ConnectX\ |reg|-4 50G MCX415A-GCAT (1x50G)
* Mellanox\ |reg| ConnectX\ |reg|-4 50G MCX416A-BCAT (2x50G)
* Mellanox\ |reg| ConnectX\ |reg|-4 50G MCX416A-GCAT (2x50G)
* Mellanox\ |reg| ConnectX\ |reg|-4 50G MCX415A-CCAT (1x100G)
* Mellanox\ |reg| ConnectX\ |reg|-4 100G MCX416A-CCAT (2x100G)
* Mellanox\ |reg| ConnectX\ |reg|-4 Lx 10G MCX4111A-XCAT (1x10G)
* Mellanox\ |reg| ConnectX\ |reg|-4 Lx 10G MCX4121A-XCAT (2x10G)
* Mellanox\ |reg| ConnectX\ |reg|-4 Lx 25G MCX4111A-ACAT (1x25G)
* Mellanox\ |reg| ConnectX\ |reg|-4 Lx 25G MCX4121A-ACAT (2x25G)
* Mellanox\ |reg| ConnectX\ |reg|-4 Lx 40G MCX4131A-BCAT (1x40G)
* Mellanox\ |reg| ConnectX\ |reg|-5 100G MCX556A-ECAT (2x100G)
* Mellanox\ |reg| ConnectX\ |reg|-5 Ex EN 100G MCX516A-CDAT (2x100G)
* Mellanox\ |reg| ConnectX\ |reg|-6 200G MCX654106A-HCAT (2x200G)
* Mellanox\ |reg| ConnectX\ |reg|-6 Dx EN 100G MCX623106AN-CDAT (2x100G)
* Mellanox\ |reg| ConnectX\ |reg|-6 Dx EN 200G MCX623105AN-VDAT (1x200G)
* Mellanox\ |reg| ConnectX\ |reg|-6 Lx EN 25G MCX631102AN-ADAT (2x25G)

Quick Start Guide on OFED/EN
----------------------------

1. Download latest Mellanox OFED/EN. For more info check the `linux prerequisites`_.


2. Install the required libraries and kernel modules either by installing
   only the required set, or by installing the entire Mellanox OFED/EN::

        ./mlnxofedinstall --upstream-libs --dpdk

3. Verify the firmware is the correct one::

        ibv_devinfo

4. Verify all ports links are set to Ethernet::

        mlxconfig -d <mst device> query | grep LINK_TYPE
        LINK_TYPE_P1                        ETH(2)
        LINK_TYPE_P2                        ETH(2)

   Link types may have to be configured to Ethernet::

        mlxconfig -d <mst device> set LINK_TYPE_P1/2=1/2/3

        * LINK_TYPE_P1=<1|2|3> , 1=Infiniband 2=Ethernet 3=VPI(auto-sense)

   For hypervisors, verify SR-IOV is enabled on the NIC::

        mlxconfig -d <mst device> query | grep SRIOV_EN
        SRIOV_EN                            True(1)

   If needed, configure SR-IOV::

        mlxconfig -d <mst device> set SRIOV_EN=1 NUM_OF_VFS=16
        mlxfwreset -d <mst device> reset

5. Restart the driver::

        /etc/init.d/openibd restart

   or::

        service openibd restart

   If link type was changed, firmware must be reset as well::

        mlxfwreset -d <mst device> reset

   For hypervisors, after reset write the sysfs number of virtual functions
   needed for the PF.

   To dynamically instantiate a given number of virtual functions (VFs)::

        echo [num_vfs] > /sys/class/infiniband/mlx5_0/device/sriov_numvfs

6. Install DPDK and you are ready to go.
   See :doc:`compilation instructions <../linux_gsg/build_dpdk>`.

Enable switchdev mode
---------------------

Switchdev mode is a mode in E-Switch, that binds between representor and VF or SF.
Representor is a port in DPDK that is connected to a VF or SF in such a way
that assuming there are no offload flows, each packet that is sent from the VF or SF
will be received by the corresponding representor. While each packet that is or SF
sent to a representor will be received by the VF or SF.
This is very useful in case of SRIOV mode, where the first packet that is sent
by the VF or SF will be received by the DPDK application which will decide if this
flow should be offloaded to the E-Switch. After offloading the flow packet
that the VF or SF that are matching the flow will not be received any more by
the DPDK application.

1. Enable SRIOV mode::

        mlxconfig -d <mst device> set SRIOV_EN=true

2. Configure the max number of VFs::

        mlxconfig -d <mst device> set NUM_OF_VFS=<num of vfs>

3. Reset the FW::

        mlxfwreset -d <mst device> reset

3. Configure the actual number of VFs::

        echo <num of vfs > /sys/class/net/<net device>/device/sriov_numvfs

4. Unbind the device (can be rebind after the switchdev mode)::

        echo -n "<device pci address" > /sys/bus/pci/drivers/mlx5_core/unbind

5. Enable switchdev mode::

        echo switchdev > /sys/class/net/<net device>/compat/devlink/mode

Sub-Function support
--------------------

Sub-Function is a portion of the PCI device, a SF netdev has its own
dedicated queues (txq, rxq).
A SF shares PCI level resources with other SFs and/or with its parent PCI function.

0. Requirement::

        OFED version >= 5.4-0.3.3.0

1. Configure SF feature::

        # Run mlxconfig on both PFs on host and ECPFs on BlueField.
        mlxconfig -d <mst device> set PER_PF_NUM_SF=1 PF_TOTAL_SF=252 PF_SF_BAR_SIZE=12

2. Enable switchdev mode::

        mlxdevm dev eswitch set pci/<DBDF> mode switchdev

3. Add SF port::

        mlxdevm port add pci/<DBDF> flavour pcisf pfnum 0 sfnum <sfnum>

        Get SFID from output: pci/<DBDF>/<SFID>

4. Modify MAC address::

        mlxdevm port function set pci/<DBDF>/<SFID> hw_addr <MAC>

5. Activate SF port::

        mlxdevm port function set pci/<DBDF>/<ID> state active

6. Devargs to probe SF device::

        auxiliary:mlx5_core.sf.<num>,dv_flow_en=1

Sub-Function representor support
--------------------------------

A SF netdev supports E-Switch representation offload
similar to PF and VF representors.
Use <sfnum> to probe SF representor::

        testpmd> port attach <PCI_BDF>,representor=sf<sfnum>,dv_flow_en=1

Performance tuning
------------------

1. Configure aggressive CQE Zipping for maximum performance::

        mlxconfig -d <mst device> s CQE_COMPRESSION=1

  To set it back to the default CQE Zipping mode use::

        mlxconfig -d <mst device> s CQE_COMPRESSION=0

2. In case of virtualization:

   - Make sure that hypervisor kernel is 3.16 or newer.
   - Configure boot with ``iommu=pt``.
   - Use 1G huge pages.
   - Make sure to allocate a VM on huge pages.
   - Make sure to set CPU pinning.

3. Use the CPU near local NUMA node to which the PCIe adapter is connected,
   for better performance. For VMs, verify that the right CPU
   and NUMA node are pinned according to the above. Run::

        lstopo-no-graphics --merge

   to identify the NUMA node to which the PCIe adapter is connected.

4. If more than one adapter is used, and root complex capabilities allow
   to put both adapters on the same NUMA node without PCI bandwidth degradation,
   it is recommended to locate both adapters on the same NUMA node.
   This in order to forward packets from one to the other without
   NUMA performance penalty.

5. Disable pause frames::

        ethtool -A <netdev> rx off tx off

6. Verify IO non-posted prefetch is disabled by default. This can be checked
   via the BIOS configuration. Please contact you server provider for more
   information about the settings.

.. note::

        On some machines, depends on the machine integrator, it is beneficial
        to set the PCI max read request parameter to 1K. This can be
        done in the following way:

        To query the read request size use::

                setpci -s <NIC PCI address> 68.w

        If the output is different than 3XXX, set it by::

                setpci -s <NIC PCI address> 68.w=3XXX

        The XXX can be different on different systems. Make sure to configure
        according to the setpci output.

7. To minimize overhead of searching Memory Regions:

   - '--socket-mem' is recommended to pin memory by predictable amount.
   - Configure per-lcore cache when creating Mempools for packet buffer.
   - Refrain from dynamically allocating/freeing memory in run-time.

Rx burst functions
------------------

There are multiple Rx burst functions with different advantages and limitations.

.. table:: Rx burst functions

   +-------------------+------------------------+---------+-----------------+------+-------+
   || Function Name    || Enabler               || Scatter|| Error Recovery || CQE || Large|
   |                   |                        |         |                 || comp|| MTU  |
   +===================+========================+=========+=================+======+=======+
   | rx_burst          | rx_vec_en=0            |   Yes   | Yes             |  Yes |  Yes  |
   +-------------------+------------------------+---------+-----------------+------+-------+
   | rx_burst_vec      | rx_vec_en=1 (default)  |   No    | if CQE comp off |  Yes |  No   |
   +-------------------+------------------------+---------+-----------------+------+-------+
   | rx_burst_mprq     || mprq_en=1             |   No    | Yes             |  Yes |  Yes  |
   |                   || RxQs >= rxqs_min_mprq |         |                 |      |       |
   +-------------------+------------------------+---------+-----------------+------+-------+
   | rx_burst_mprq_vec || rx_vec_en=1 (default) |   No    | if CQE comp off |  Yes |  Yes  |
   |                   || mprq_en=1             |         |                 |      |       |
   |                   || RxQs >= rxqs_min_mprq |         |                 |      |       |
   +-------------------+------------------------+---------+-----------------+------+-------+

.. _mlx5_offloads_support:

Supported hardware offloads
---------------------------

.. table:: Minimal SW/HW versions for queue offloads

   ============== ===== ===== ========= ===== ========== =============
   Offload        DPDK  Linux rdma-core OFED   firmware   hardware
   ============== ===== ===== ========= ===== ========== =============
   common base    17.11  4.14    16     4.2-1 12.21.1000 ConnectX-4
   checksums      17.11  4.14    16     4.2-1 12.21.1000 ConnectX-4
   Rx timestamp   17.11  4.14    16     4.2-1 12.21.1000 ConnectX-4
   TSO            17.11  4.14    16     4.2-1 12.21.1000 ConnectX-4
   LRO            19.08  N/A     N/A    4.6-4 16.25.6406 ConnectX-5
   Tx scheduling  20.08  N/A     N/A    5.1-2 22.28.2006 ConnectX-6 Dx
   Buffer Split   20.11  N/A     N/A    5.1-2 16.28.2006 ConnectX-5
   ============== ===== ===== ========= ===== ========== =============

.. table:: Minimal SW/HW versions for rte_flow offloads

   +-----------------------+-----------------+-----------------+
   | Offload               | with E-Switch   | with NIC        |
   +=======================+=================+=================+
   | Count                 | | DPDK 19.05    | | DPDK 19.02    |
   |                       | | OFED 4.6      | | OFED 4.6      |
   |                       | | rdma-core 24  | | rdma-core 23  |
   |                       | | ConnectX-5    | | ConnectX-5    |
   +-----------------------+-----------------+-----------------+
   | Drop                  | | DPDK 19.05    | | DPDK 18.11    |
   |                       | | OFED 4.6      | | OFED 4.5      |
   |                       | | rdma-core 24  | | rdma-core 23  |
   |                       | | ConnectX-5    | | ConnectX-4    |
   +-----------------------+-----------------+-----------------+
   | Queue / RSS           | |               | | DPDK 18.11    |
   |                       | |     N/A       | | OFED 4.5      |
   |                       | |               | | rdma-core 23  |
   |                       | |               | | ConnectX-4    |
   +-----------------------+-----------------+-----------------+
   | Shared action         | |               | |               |
   |                       | | :numref:`sact`| | :numref:`sact`|
   |                       | |               | |               |
   |                       | |               | |               |
   +-----------------------+-----------------+-----------------+
   | | VLAN                | | DPDK 19.11    | | DPDK 19.11    |
   | | (of_pop_vlan /      | | OFED 4.7-1    | | OFED 4.7-1    |
   | | of_push_vlan /      | | ConnectX-5    | | ConnectX-5    |
   | | of_set_vlan_pcp /   | |               | |               |
   | | of_set_vlan_vid)    | |               | |               |
   +-----------------------+-----------------+-----------------+
   | | VLAN                | | DPDK 21.05    | |               |
   | | ingress and /       | | OFED 5.3      | |    N/A        |
   | | of_push_vlan /      | | ConnectX-6 Dx | |               |
   +-----------------------+-----------------+-----------------+
   | | VLAN                | | DPDK 21.05    | |               |
   | | egress and /        | | OFED 5.3      | |    N/A        |
   | | of_pop_vlan /       | | ConnectX-6 Dx | |               |
   +-----------------------+-----------------+-----------------+
   | Encapsulation         | | DPDK 19.05    | | DPDK 19.02    |
   | (VXLAN / NVGRE / RAW) | | OFED 4.7-1    | | OFED 4.6      |
   |                       | | rdma-core 24  | | rdma-core 23  |
   |                       | | ConnectX-5    | | ConnectX-5    |
   +-----------------------+-----------------+-----------------+
   | Encapsulation         | | DPDK 19.11    | | DPDK 19.11    |
   | GENEVE                | | OFED 4.7-3    | | OFED 4.7-3    |
   |                       | | rdma-core 27  | | rdma-core 27  |
   |                       | | ConnectX-5    | | ConnectX-5    |
   +-----------------------+-----------------+-----------------+
   | Tunnel Offload        | |  DPDK 20.11   | | DPDK 20.11    |
   |                       | |  OFED 5.1-2   | | OFED 5.1-2    |
   |                       | |  rdma-core 32 | | N/A           |
   |                       | |  ConnectX-5   | | ConnectX-5    |
   +-----------------------+-----------------+-----------------+
   | | Header rewrite      | | DPDK 19.05    | | DPDK 19.02    |
   | | (set_ipv4_src /     | | OFED 4.7-1    | | OFED 4.7-1    |
   | | set_ipv4_dst /      | | rdma-core 24  | | rdma-core 24  |
   | | set_ipv6_src /      | | ConnectX-5    | | ConnectX-5    |
   | | set_ipv6_dst /      | |               | |               |
   | | set_tp_src /        | |               | |               |
   | | set_tp_dst /        | |               | |               |
   | | dec_ttl /           | |               | |               |
   | | set_ttl /           | |               | |               |
   | | set_mac_src /       | |               | |               |
   | | set_mac_dst)        | |               | |               |
   +-----------------------+-----------------+-----------------+
   | | Header rewrite      | | DPDK 20.02    | | DPDK 20.02    |
   | | (set_dscp)          | | OFED 5.0      | | OFED 5.0      |
   | |                     | | rdma-core 24  | | rdma-core 24  |
   | |                     | | ConnectX-5    | | ConnectX-5    |
   +-----------------------+-----------------+-----------------+
   | Jump                  | | DPDK 19.05    | | DPDK 19.02    |
   |                       | | OFED 4.7-1    | | OFED 4.7-1    |
   |                       | | rdma-core 24  | | N/A           |
   |                       | | ConnectX-5    | | ConnectX-5    |
   +-----------------------+-----------------+-----------------+
   | Mark / Flag           | | DPDK 19.05    | | DPDK 18.11    |
   |                       | | OFED 4.6      | | OFED 4.5      |
   |                       | | rdma-core 24  | | rdma-core 23  |
   |                       | | ConnectX-5    | | ConnectX-4    |
   +-----------------------+-----------------+-----------------+
   | Meta data             | |  DPDK 19.11   | | DPDK 19.11    |
   |                       | |  OFED 4.7-3   | | OFED 4.7-3    |
   |                       | |  rdma-core 26 | | rdma-core 26  |
   |                       | |  ConnectX-5   | | ConnectX-5    |
   +-----------------------+-----------------+-----------------+
   | Port ID               | | DPDK 19.05    |     | N/A       |
   |                       | | OFED 4.7-1    |     | N/A       |
   |                       | | rdma-core 24  |     | N/A       |
   |                       | | ConnectX-5    |     | N/A       |
   +-----------------------+-----------------+-----------------+
   | Hairpin               | |               | | DPDK 19.11    |
   |                       | |     N/A       | | OFED 4.7-3    |
   |                       | |               | | rdma-core 26  |
   |                       | |               | | ConnectX-5    |
   +-----------------------+-----------------+-----------------+
   | 2-port Hairpin        | |               | | DPDK 20.11    |
   |                       | |     N/A       | | OFED 5.1-2    |
   |                       | |               | | N/A           |
   |                       | |               | | ConnectX-5    |
   +-----------------------+-----------------+-----------------+
   | Metering              | |  DPDK 19.11   | | DPDK 19.11    |
   |                       | |  OFED 4.7-3   | | OFED 4.7-3    |
   |                       | |  rdma-core 26 | | rdma-core 26  |
   |                       | |  ConnectX-5   | | ConnectX-5    |
   +-----------------------+-----------------+-----------------+
   | ASO Metering          | |  DPDK 21.05   | | DPDK 21.05    |
   |                       | |  OFED 5.3     | | OFED 5.3      |
   |                       | |  rdma-core 33 | | rdma-core 33  |
   |                       | |  ConnectX-6 Dx| | ConnectX-6 Dx |
   +-----------------------+-----------------+-----------------+
   | Metering Hierarchy    | |  DPDK 21.08   | | DPDK 21.08    |
   |                       | |  OFED 5.3     | | OFED 5.3      |
   |                       | |  N/A          | | N/A           |
   |                       | |  ConnectX-6 Dx| | ConnectX-6 Dx |
   +-----------------------+-----------------+-----------------+
   | Sampling              | |  DPDK 20.11   | | DPDK 20.11    |
   |                       | |  OFED 5.1-2   | | OFED 5.1-2    |
   |                       | |  rdma-core 32 | | N/A           |
   |                       | |  ConnectX-5   | | ConnectX-5    |
   +-----------------------+-----------------+-----------------+
   | Encapsulation         | |  DPDK 21.02   | | DPDK 21.02    |
   | GTP PSC               | |  OFED 5.2     | | OFED 5.2      |
   |                       | |  rdma-core 35 | | rdma-core 35  |
   |                       | |  ConnectX-6 Dx| | ConnectX-6 Dx |
   +-----------------------+-----------------+-----------------+
   | Encapsulation         | | DPDK 21.02    | | DPDK 21.02    |
   | GENEVE TLV option     | | OFED 5.2      | | OFED 5.2      |
   |                       | | rdma-core 34  | | rdma-core 34  |
   |                       | | ConnectX-6 Dx | | ConnectX-6 Dx |
   +-----------------------+-----------------+-----------------+
   | Modify Field          | | DPDK 21.02    | | DPDK 21.02    |
   |                       | | OFED 5.2      | | OFED 5.2      |
   |                       | | rdma-core 35  | | rdma-core 35  |
   |                       | | ConnectX-5    | | ConnectX-5    |
   +-----------------------+-----------------+-----------------+
   | Connection tracking   | |               | | DPDK 21.05    |
   |                       | |     N/A       | | OFED 5.3      |
   |                       | |               | | rdma-core 35  |
   |                       | |               | | ConnectX-6 Dx |
   +-----------------------+-----------------+-----------------+

.. table:: Minimal SW/HW versions for shared action offload
   :name: sact

   +-----------------------+-----------------+-----------------+
   | Shared Action         | with E-Switch   | with NIC        |
   +=======================+=================+=================+
   | RSS                   | |               | | DPDK 20.11    |
   |                       | |     N/A       | | OFED 5.2      |
   |                       | |               | | rdma-core 33  |
   |                       | |               | | ConnectX-5    |
   +-----------------------+-----------------+-----------------+
   | Age                   | | DPDK 20.11    | | DPDK 20.11    |
   |                       | | OFED 5.2      | | OFED 5.2      |
   |                       | | rdma-core 32  | | rdma-core 32  |
   |                       | | ConnectX-6 Dx | | ConnectX-6 Dx |
   +-----------------------+-----------------+-----------------+
   | Count                 | | DPDK 21.05    | | DPDK 21.05    |
   |                       | | OFED 4.6      | | OFED 4.6      |
   |                       | | rdma-core 24  | | rdma-core 23  |
   |                       | | ConnectX-5    | | ConnectX-5    |
   +-----------------------+-----------------+-----------------+

Notes for metadata
------------------

MARK and META items are interrelated with datapath - they might move from/to
the applications in mbuf fields. Hence, zero value for these items has the
special meaning - it means "no metadata are provided", not zero values are
treated by applications and PMD as valid ones.

Moreover in the flow engine domain the value zero is acceptable to match and
set, and we should allow to specify zero values as rte_flow parameters for the
META and MARK items and actions. In the same time zero mask has no meaning and
should be rejected on validation stage.

Notes for rte_flow
------------------

Flows are not cached in the driver.
When stopping a device port, all the flows created on this port from the
application will be flushed automatically in the background.
After stopping the device port, all flows on this port become invalid and
not represented in the system.
All references to these flows held by the application should be discarded
directly but neither destroyed nor flushed.

The application should re-create the flows as required after the port restart.

Notes for testpmd
-----------------

Compared to librte_net_mlx4 that implements a single RSS configuration per
port, librte_net_mlx5 supports per-protocol RSS configuration.

Since ``testpmd`` defaults to IP RSS mode and there is currently no
command-line parameter to enable additional protocols (UDP and TCP as well
as IP), the following commands must be entered from its CLI to get the same
behavior as librte_net_mlx4::

   > port stop all
   > port config all rss all
   > port start all

Usage example
-------------

This section demonstrates how to launch **testpmd** with Mellanox
ConnectX-4/ConnectX-5/ConnectX-6/BlueField devices managed by librte_net_mlx5.

#. Load the kernel modules::

      modprobe -a ib_uverbs mlx5_core mlx5_ib

   Alternatively if MLNX_OFED/MLNX_EN is fully installed, the following script
   can be run::

      /etc/init.d/openibd restart

   .. note::

      User space I/O kernel modules (uio and igb_uio) are not used and do
      not have to be loaded.

#. Make sure Ethernet interfaces are in working order and linked to kernel
   verbs. Related sysfs entries should be present::

      ls -d /sys/class/net/*/device/infiniband_verbs/uverbs* | cut -d / -f 5

   Example output::

      eth30
      eth31
      eth32
      eth33

#. Optionally, retrieve their PCI bus addresses for to be used with the allow list::

      {
          for intf in eth2 eth3 eth4 eth5;
          do
              (cd "/sys/class/net/${intf}/device/" && pwd -P);
          done;
      } |
      sed -n 's,.*/\(.*\),-a \1,p'

   Example output::

      -a 0000:05:00.1
      -a 0000:06:00.0
      -a 0000:06:00.1
      -a 0000:05:00.0

#. Request huge pages::

      dpdk-hugepages.py --setup 2G

#. Start testpmd with basic parameters::

      dpdk-testpmd -l 8-15 -n 4 -a 05:00.0 -a 05:00.1 -a 06:00.0 -a 06:00.1 -- --rxq=2 --txq=2 -i

   Example output::

      [...]
      EAL: PCI device 0000:05:00.0 on NUMA socket 0
      EAL:   probe driver: 15b3:1013 librte_net_mlx5
      PMD: librte_net_mlx5: PCI information matches, using device "mlx5_0" (VF: false)
      PMD: librte_net_mlx5: 1 port(s) detected
      PMD: librte_net_mlx5: port 1 MAC address is e4:1d:2d:e7:0c:fe
      EAL: PCI device 0000:05:00.1 on NUMA socket 0
      EAL:   probe driver: 15b3:1013 librte_net_mlx5
      PMD: librte_net_mlx5: PCI information matches, using device "mlx5_1" (VF: false)
      PMD: librte_net_mlx5: 1 port(s) detected
      PMD: librte_net_mlx5: port 1 MAC address is e4:1d:2d:e7:0c:ff
      EAL: PCI device 0000:06:00.0 on NUMA socket 0
      EAL:   probe driver: 15b3:1013 librte_net_mlx5
      PMD: librte_net_mlx5: PCI information matches, using device "mlx5_2" (VF: false)
      PMD: librte_net_mlx5: 1 port(s) detected
      PMD: librte_net_mlx5: port 1 MAC address is e4:1d:2d:e7:0c:fa
      EAL: PCI device 0000:06:00.1 on NUMA socket 0
      EAL:   probe driver: 15b3:1013 librte_net_mlx5
      PMD: librte_net_mlx5: PCI information matches, using device "mlx5_3" (VF: false)
      PMD: librte_net_mlx5: 1 port(s) detected
      PMD: librte_net_mlx5: port 1 MAC address is e4:1d:2d:e7:0c:fb
      Interactive-mode selected
      Configuring Port 0 (socket 0)
      PMD: librte_net_mlx5: 0x8cba80: TX queues number update: 0 -> 2
      PMD: librte_net_mlx5: 0x8cba80: RX queues number update: 0 -> 2
      Port 0: E4:1D:2D:E7:0C:FE
      Configuring Port 1 (socket 0)
      PMD: librte_net_mlx5: 0x8ccac8: TX queues number update: 0 -> 2
      PMD: librte_net_mlx5: 0x8ccac8: RX queues number update: 0 -> 2
      Port 1: E4:1D:2D:E7:0C:FF
      Configuring Port 2 (socket 0)
      PMD: librte_net_mlx5: 0x8cdb10: TX queues number update: 0 -> 2
      PMD: librte_net_mlx5: 0x8cdb10: RX queues number update: 0 -> 2
      Port 2: E4:1D:2D:E7:0C:FA
      Configuring Port 3 (socket 0)
      PMD: librte_net_mlx5: 0x8ceb58: TX queues number update: 0 -> 2
      PMD: librte_net_mlx5: 0x8ceb58: RX queues number update: 0 -> 2
      Port 3: E4:1D:2D:E7:0C:FB
      Checking link statuses...
      Port 0 Link Up - speed 40000 Mbps - full-duplex
      Port 1 Link Up - speed 40000 Mbps - full-duplex
      Port 2 Link Up - speed 10000 Mbps - full-duplex
      Port 3 Link Up - speed 10000 Mbps - full-duplex
      Done
      testpmd>

How to dump flows
-----------------

This section demonstrates how to dump flows. Currently, it's possible to dump
all flows with assistance of external tools.

#. 2 ways to get flow raw file:

   - Using testpmd CLI:

   .. code-block:: console

       To dump all flows:
       testpmd> flow dump <port> all <output_file>
       and dump one flow:
       testpmd> flow dump <port> rule <rule_id> <output_file>

   - call rte_flow_dev_dump api:

   .. code-block:: console

       rte_flow_dev_dump(port, flow, file, NULL);

#. Dump human-readable flows from raw file:

   Get flow parsing tool from: https://github.com/Mellanox/mlx_steering_dump

   .. code-block:: console

       mlx_steering_dump.py -f <output_file> -flowptr <flow_ptr>

How to share a meter between ports in the same switch domain
------------------------------------------------------------

This section demonstrates how to use the shared meter. A meter M can be created
on port X and to be shared with a port Y on the same switch domain by the next way:

.. code-block:: console

   flow create X ingress transfer pattern eth / port_id id is Y / end actions meter mtr_id M / end

How to use meter hierarchy
--------------------------

This section demonstrates how to create and use a meter hierarchy.
A termination meter M can be the policy green action of another termination meter N.
The two meters are chained together as a chain. Using meter N in a flow will apply
both the meters in hierarchy on that flow.

.. code-block:: console

   add port meter policy 0 1 g_actions queue index 0 / end y_actions end r_actions drop / end
   create port meter 0 M 1 1 yes 0xffff 1 0
   add port meter policy 0 2 g_actions meter mtr_id M / end y_actions end r_actions drop / end
   create port meter 0 N 2 2 yes 0xffff 1 0
   flow create 0 ingress group 1 pattern eth / end actions meter mtr_id N / end
