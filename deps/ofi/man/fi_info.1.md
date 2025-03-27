---
layout: page
title: fi_info(1)
tagline: Libfabric Programmer's Manual
---
{% include JB/setup %}


# NAME

fi_info  \- Simple utility to query for fabric interfaces


# SYNOPSIS
```
 fi_info [OPTIONS]
```

# DESCRIPTION

The fi_info utility can be used to query for available fabric interfaces. The
utility supports filtering based on a number of options such as endpoint type,
provider name, or supported modes. Additionally, fi_info can also be used to
discover the environment variables that can be used to tune provider specific
parameters. If no filters are specified, then all available fabric interfaces
for all providers and endpoint types will be returned.

# OPTIONS

## Filtering

*-n, --node=\<NAME\>*
: Node name or address used to filter interfaces. Only interfaces which can
reach the given node or address will respond.

*-P, --port=\<PORT\>*
: Port number used to filter interfaces.

*-c, --caps=\<CAP1|CAP2\>..*
: Pipe separated list of capabilities used to filter interfaces. Only
interfaces supporting all of the given capabilities will respond. For more
information on capabilities, see fi_getinfo(3).

*-m, --mode=\<MOD1|MOD2\>..*
: Pipe separated list of modes used to filter interfaces. Only interfaces
supporting all of the given modes will respond. For more information on, modes
see fi_getinfo(3).

*-t, --ep_type=\<EPTYPE\>*
: Specifies the type of fabric interface communication desired. For example,
specifying FI_EP_DGRAM would return only interfaces which support unreliable
datagram. For more information on endpoint types, see fi_endpoint(3).

*-a, --addr_format=\<FMT\>*
: Filter fabric interfaces by their address format. For example,
specifying FI_SOCKADDR_IN would return only interfaces which use sockaddr_in
structures for addressing. For more information on address formats, see
fi_getinfo(3).

*-p, --provider=\<PROV\>*
: Filter fabric interfaces by the provider implementation. For a list of
providers, see the `--list` option.

*-d, --domain=\<DOMAIN\>*
: Filter interfaces to only those with the given domain name.

*-f, --fabric=\<FABRIC\>*
: Filter interfaces to only those with the given fabric name.

## Discovery

*-e, --env*
: List libfabric related environment variables which can be used to enable extra
configuration or tuning.

*-g [filter]*
: Same as -e option, with output limited to environment variables containing
filter as a substring.

*-l, --list*
: List available libfabric providers.

*-v, --verbose*
: By default, fi_info will display a summary of each of the interfaces
discovered. If the verbose option is enabled, then all of the contents of the
fi_info structure are displayed. For more information on the data contained in
the fi_info structure, see fi_getinfo(3).

*--version*
: Display versioning information.

# USAGE EXAMPLES

```
$ fi_info -p verbs -t FI_EP_DGRAM
```

This will respond with all fabric interfaces that use endpoint type
FI_EP_DGRAM with the verbs provider.

```
fi_info -c 'FI_MSG|FI_READ|FI_RMA'
```

This will respond with all fabric interfaces that can support
FI_MSG|FI_READ|FI_RMA capabilities.

# OUTPUT

By default fi_info will output a summary of the fabric interfaces discovered:

```
$ ./fi_info -p verbs -t FI_EP_DGRAM
provider: verbs
    fabric: IB-0xfe80000000000000
    domain: mlx5_0-dgram
    version: 116.0
    type: FI_EP_DGRAM
    protocol: FI_PROTO_IB_UD

$ ./fi_info -p tcp
provider: tcp
    fabric: 192.168.7.0/24
    domain: eth0
    version: 116.0
    type: FI_EP_MSG
    protocol: FI_PROTO_SOCK_TCP
```

To see the full fi_info structure, specify the `-v` option.

```
fi_info:
    caps: [ FI_MSG, FI_RMA, FI_TAGGED, FI_ATOMIC, FI_READ, FI_WRITE, FI_RECV, FI_SEND, FI_REMOTE_READ, FI_REMOTE_WRITE, FI_MULTI_RECV, FI_RMA_EVENT, FI_SOURCE, FI_DIRECTED_RECV ]
    mode: [  ]
    addr_format: FI_ADDR_IB_UD
    src_addrlen: 32
    dest_addrlen: 0
    src_addr: fi_addr_ib_ud://:::0/0/0/0
    dest_addr: (null)
    handle: (nil)
    fi_tx_attr:
        caps: [ FI_MSG, FI_RMA, FI_TAGGED, FI_ATOMIC, FI_READ, FI_WRITE, FI_SEND ]
        mode: [  ]
        op_flags: [  ]
        msg_order: [ FI_ORDER_RAR, FI_ORDER_RAW, FI_ORDER_RAS, FI_ORDER_WAW, FI_ORDER_WAS, FI_ORDER_SAW, FI_ORDER_SAS, FI_ORDER_RMA_RAR, FI_ORDER_RMA_RAW, FI_ORDER_RMA_WAW, FI_ORDER_ATOMIC_RAR, FI_ORDER_ATOMIC_RAW, FI_ORDER_ATOMIC_WAR, FI_ORDER_ATOMIC_WAW ]
        comp_order: [ FI_ORDER_NONE ]
        inject_size: 3840
        size: 1024
        iov_limit: 4
        rma_iov_limit: 4
        tclass: 0x0
    fi_rx_attr:
        caps: [ FI_MSG, FI_RMA, FI_TAGGED, FI_ATOMIC, FI_RECV, FI_REMOTE_READ, FI_REMOTE_WRITE, FI_MULTI_RECV, FI_RMA_EVENT, FI_SOURCE, FI_DIRECTED_RECV ]
        mode: [  ]
        op_flags: [  ]
        msg_order: [ FI_ORDER_RAR, FI_ORDER_RAW, FI_ORDER_RAS, FI_ORDER_WAW, FI_ORDER_WAS, FI_ORDER_SAW, FI_ORDER_SAS, FI_ORDER_RMA_RAR, FI_ORDER_RMA_RAW, FI_ORDER_RMA_WAW, FI_ORDER_ATOMIC_RAR, FI_ORDER_ATOMIC_RAW, FI_ORDER_ATOMIC_WAR, FI_ORDER_ATOMIC_WAW ]
        comp_order: [ FI_ORDER_NONE ]
        total_buffered_recv: 0
        size: 1024
        iov_limit: 4
    fi_ep_attr:
        type: FI_EP_RDM
        protocol: FI_PROTO_RXD
        protocol_version: 1
        max_msg_size: 18446744073709551615
        msg_prefix_size: 0
        max_order_raw_size: 18446744073709551615
        max_order_war_size: 0
        max_order_waw_size: 18446744073709551615
        mem_tag_format: 0xaaaaaaaaaaaaaaaa
        tx_ctx_cnt: 1
        rx_ctx_cnt: 1
        auth_key_size: 0
    fi_domain_attr:
        domain: 0x0
        name: mlx5_0-dgram
        threading: FI_THREAD_SAFE
        control_progress: FI_PROGRESS_MANUAL
        data_progress: FI_PROGRESS_MANUAL
        resource_mgmt: FI_RM_ENABLED
        av_type: FI_AV_UNSPEC
        mr_mode: [  ]
        mr_key_size: 8
        cq_data_size: 8
        cq_cnt: 128
        ep_cnt: 128
        tx_ctx_cnt: 1
        rx_ctx_cnt: 1
        max_ep_tx_ctx: 1
        max_ep_rx_ctx: 1
        max_ep_stx_ctx: 0
        max_ep_srx_ctx: 0
        cntr_cnt: 0
        mr_iov_limit: 1
        caps: [  ]
        mode: [  ]
        auth_key_size: 0
        max_err_data: 0
        mr_cnt: 0
        tclass: 0x0
    fi_fabric_attr:
        name: IB-0xfe80000000000000
        prov_name: verbs;ofi_rxd
        prov_version: 116.0
        api_version: 1.16
    nic:
        fi_device_attr:
            name: mlx5_0
            device_id: 0x101b
            device_version: 0
            vendor_id: 0x02c9
            driver: (null)
            firmware: 20.33.1048
        fi_bus_attr:
            bus_type: FI_BUS_UNKNOWN
        fi_link_attr:
            address: (null)
            mtu: 4096
            speed: 0
            state: FI_LINK_UP
            network_type: InfiniBand
```

To see libfabric related environment variables `-e` option.

```
$ ./fi_info -e
# FI_LOG_INTERVAL: Integer
# Delay in ms between rate limited log messages (default 2000)

# FI_LOG_LEVEL: String
# Specify logging level: warn, trace, info, debug (default: warn)

# FI_LOG_PROV: String
# Specify specific provider to log (default: all)

# FI_PROVIDER: String
# Only use specified provider (default: all available)
```

To see libfabric related environment variables with substring use `-g` option.

```
$ ./fi_info -g tcp
# FI_OFI_RXM_DEF_TCP_WAIT_OBJ: String
# ofi_rxm: See def_wait_obj for description.  If set, this overrides the def_wait_obj when running over the tcp provider.  See def_wait_obj for valid values. (default: UNSPEC, tcp provider will select).

# FI_TCP_IFACE: String
# tcp: Specify interface name

# FI_TCP_PORT_LOW_RANGE: Integer
# tcp: define port low range

# FI_TCP_PORT_HIGH_RANGE: Integer
# tcp: define port high range

# FI_TCP_TX_SIZE: size_t
# tcp: define default tx context size (default: 256)

# FI_TCP_RX_SIZE: size_t
# tcp: define default rx context size (default: 256)

# FI_TCP_NODELAY: Boolean (0/1, on/off, true/false, yes/no)
# tcp: overrides default TCP_NODELAY socket setting

# FI_TCP_STAGING_SBUF_SIZE: Integer
# tcp: size of buffer used to coalesce iovec's or send requests before posting to the kernel, set to 0 to disable

# FI_TCP_PREFETCH_RBUF_SIZE: Integer
# tcp: size of buffer used to prefetch received data from the kernel, set to 0 to disable

# FI_TCP_ZEROCOPY_SIZE: size_t
# tcp: lower threshold where zero copy transfers will be used, if supported by the platform, set to -1 to disable (default: 18446744073709551615)
```

# SEE ALSO

[`fi_getinfo(3)`](fi_getinfo.3.html),
[`fi_endpoint(3)`](fi_endpoint.3.html)
