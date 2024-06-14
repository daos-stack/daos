# DAOS Common Tasks

This section describes some of the common tasks handled by admins at a high level. See [System Deployment](./deployment.md#system-deployment), [DAOS System Administration](./administration.md#daos-system-administration), and [Pool Operations](./pool_operations.md#pool-operations) for more detailed explanations about each step.

## Single host setup with PMEM and NVMe

1. Check PMEM and NVMe are discovered by the system. Format and reset them.
2. Check network configuration. Check the `fabric_iface` you want to use is active.
3. Install `daos-server` and `daos-client` RPMs.
4. Generate certificate files.
5. Copy one of the example configs from `utils/config/examples` to
`/etc/daos` and adjust it based on the environment. E.g., `access_points`,
`class`.
6. Check that the directory where the log files will be created exists. E.g.,
`control_log_file`, `log_file` field in `engines` section.
7. Start `daos_server`.
8. Use `dmg config generate` to generate the config file that contains PMEM and
NVMe.
9. Define the certificate files in the server config.
10. Start server with the generated config file.
11. Check that the server is waiting for SCM format. Call `dmg storage format`.
12. Create a small pool; E.g., ~500MB.
13. Define the certificate files in the agent config.
14. Start agent.
15. Create a POSIX container with the `daos` command.
16. Mount the container with dfuse.
17. Add a large file that's less than the 500MB pool size into the container.
18. Call `dmg pool query` and check that the free size has declined.

## Multiple host setup with PMEM and NVMe

1. Check PMEM and NVMe configurations are homogeneous. I.e., same number of
disks, size, address, etc.
2. Check network configuration. Check that both the server and the client hosts
can communicate with the network interface.
3. Install the same version of `daos-server` and `daos-client` RPMs. Install `daos-server`
to server hosts and `daos-client` to client hosts.
4. Generate certificate files and distribute them to all the hosts.
5. Copy one of the example configs from `utils/config/examples` to
`/etc/daos` of one of the server hosts and adjust it based on the environment.
E.g., `access_points`, `class`.
6. Check that the directory where the log files will be created exists. E.g.,
`control_log_file`, `log_file` field in `engines` section.
7. Start `daos_server`.
8. Use `dmg config generate` to generate the config file that contains PMEM and
NVMe.
9. Distribute the config file to `/etc/daos` of all hosts.
10. Start server on all the hosts.
11. Check that the server is waiting for SCM format. Call `dmg storage format` against all
server hosts.
12. Check that the servers are running on all the hosts with `dmg system query
--verbose`.
13. Create a small pool; E.g., ~500MB.
14. Define the certificate files in the agent config in the client host.
15. Start agent.
16. Create a POSIX container with the `daos` command.
17. Mount the container with dfuse.
18. Add a large file that's less than the 500MB pool size into the container.
19. Call `dmg pool query` and check that the free size has declined.

## Pool size management

1. Start DAOS server with PMEM + NVMe and format.
2. Create a pool with a size percentage. For example,
```
dmg pool create --size=50%
```
The percentage is applied to the usable space.

## Run dmg remotely

1. Start DAOS server on one host.
2. Create a file that specifies the server host in `/etc/daos`. It's usually
called `daos_control.yml`. Add the following:
```
hostlist:
- <server_host>
name: <group_name>
port: 10001
transport_config:
  allow_insecure: false
  ca_cert: /etc/daos/certs/daosCA.crt
  cert: /etc/daos/certs/admin.crt
  key: /etc/daos/certs/admin.key
```
`server_host` is the hostname where the server is running. `group_name` is
usually `daos_server`. Match the `port` field defined in the server config.
Adjust `transport_config` accordingly.

3. `dmg` should be able to talk to the server.

## Server config technique

- Use the network interface, PMEM, and NVMe from the same NUMA Socket ID for
the best performance.
- Call `dmg storage scan --verbose` to see the list of devices and their Socket
ID.
- Call `dmg network scan -p all` to see the provider, interface, and their
associated Socket ID.
- Use `control_log_mask: ERROR` to show only necessary messages in status.

## Multiple server ranks in a single host

- Specify multiple engines under `engines` section. Each section starts with a
dash `-`.
- Each engine must use different `fabric_iface_port`. The port numbers should
differ by at least 100.
- Each engine must have a different `log_file`.
- Use different `fabric_iface` for the best performance.
- Each engine must have unique `scm_mount`, `scm_list`, and `bdev_list`.

## Change fabric provider on a DAOS system

1. Stop all DAOS client I/O.
1. Evict client pool handles to ensure I/O has stopped.
1. Shut down all `daos_server` processes.
1. Update the fabric provider and interfaces in all `daos_server` configuration files. All `daos_server` configurations must use the same fabric provider.
1. Restart all `daos_server` processes to re-load the configuration file.
1. Ensure all ranks have re-joined by running `dmg system query`. If some ranks fail to join, check logs to troubleshoot.
1. After all ranks have joined, restart `daos_agent` processes on client nodes, or alternately send SIGUSR2 to all of these processes to refresh network information.
