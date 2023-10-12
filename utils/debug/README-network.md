# Useful URLs

[Testing Early InfiniBand RDMA operation](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/7/html/networking_guide/sec-testing_early_infiniband_rdma_operation)
[Testing early InfiniBand RDMA operations](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/8/html/configuring_infiniband_and_rdma_networks/testing-infiniband-networks_configuring-infiniband-and-rdma-networks)
[InfiniBand Utilities](https://docs.nvidia.com/networking/display/ufmsdnappcliv4140/infiniband+utilities)
[ROCE DEBUG FLOW FOR LINUX](https://enterprise-support.nvidia.com/s/article/RoCE-Debug-Flow-for-Linux)
[PERFORMANCE TUNING FOR MELLANOX ADAPTERS](https://enterprise-support.nvidia.com/s/article/performance-tuning-for-mellanox-adapters)
[Testing Early InfiniBand RDMA operation](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/7/html/networking_guide/sec-testing_early_infiniband_rdma_operation)
[Allow Infiniband and RDMA Operations To Run Without Root Privileges](https://access.redhat.com/solutions/5929621)
[NVIDIA MLNX_OFED Documentation v23.07](https://docs.nvidia.com/nvidia-mlnx-ofed-documentation-v23-07.pdf)
[Quick and simple pingpong test for libfabric](https://ofiwg.github.io/libfabric/v1.6.1/man/fi_pingpong.1.html)
[Diagnosing Performance - Mercury](https://mercury-hpc.github.io/user/perf/)
[HOWTO FIND MELLANOX ADAPTER TYPE AND FIRMWARE/DRIVER VERSION (LINUX)](https://enterprise-support.nvidia.com/s/article/howto-find-mellanox-adapter-type-and-firmware-driver-version--linux-x)


# Miscellaneous Useful Commands

To display the mellanox ofed version:
- `ofed_info -s`

To display mellanox firmware version:
- `mlxfwmanager -d <pci add> --query`
The pci addr could be found with lspci or lshw.

To have the ethernet interfaces associated to ib devices:
- `ibdev2netdev -v`

To get the ip of NIC:
- `ip add show dev <if name>`

To have the list of ib devices and there GIDs:
- `ibv_devices`
- `show_gids`

To have detailed information about ib devices:
- `ibv_devinfo -d <dev name>`
- `ibstat <dev name>`
- `ibvstatus <dev name>`

To have information about network topology:
- `ibswitches`

To test RDMA communications with ib network:
- `ibping`: require to be run as root or to have write privilege on devices `/dev/infiniband/umad*`

To test IB Verbs with `ibv_rc_pingpong`:
- client: `ibv_rc_pingpong -d <ib device> -i <ib index> -g <server gid> -p <server port> <server ip>`
- server: `ibv_rc_pingpong -d <ib device> -i <ib index> -g <server gid> -p <server port>`

To test RDAM CM connection:
- client: `ucmatose -s <server ip>`
- server: `ucmatose`

To test libfabric:
- client: `fi_pingpong -p 'verbs;ofi_rxm' -P <server port> -S 0 -e rdm <server ip>`
- server: `fi_pingpong -d <dev name> -p 'verbs;ofi_rxm' -B <server port> -S 0 -e rdm`

To have the supported libfabric configuration:
- `fi_info`

To test Mercury:
- client: `scp <server ip>:port.cfg . && hg_rate -c ofi -p 'verbs;ofi_rxm' -V -d <if name>`
- server: `hg_perf_server -c ofi -p 'verbs;ofi_rxm' -d <if name> -V -b`
On the server a file `port.cfg` is generated which should be copied on the client.

To test CART:
- `self-test -u --no-sync --group-name daos_server --message-size '(0 0)' --repetitions 1 --endpoint "0:0-7" `
- To run without the agent, a configuration file of the CART configuration needs to be provided to
  `self_test`.   More info on the format of this configuration file and how it could be generated
  could be find in the material of the ticket DAOS-17109.
